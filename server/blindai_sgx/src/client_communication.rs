// Copyright 2022 Mithril Security. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

use futures::StreamExt;
use log::*;
use num_traits::FromPrimitive;
use prost::Message;
use ring::digest;
use ring_compat::signature::Signer;
use secured_exchange::exchange_server::Exchange;
#[cfg(not(target_env = "sgx"))]
use std::sync::Mutex;
#[cfg(target_env = "sgx")]
use std::sync::SgxMutex as Mutex;
use std::{
    collections::HashMap,
    convert::TryInto,
    mem::size_of,
    str::FromStr,
    sync::Arc,
    time::{Instant, SystemTime},
    vec::Vec,
};

use futures::StreamExt;
use num_traits::FromPrimitive;
use prost::Message;
use ring_compat::signature::Signer;
use secured_exchange::exchange_server::Exchange;
use tonic::{Request, Response, Status};
use uuid::Uuid;

use crate::{
    identity::MyIdentity,
    model::{InferenceModel, ModelDatumType},
    telemetry::{self, TelemetryEventProps},
};
use secured_exchange::*;

pub mod secured_exchange {
    tonic::include_proto!("securedexchange");
}

pub(crate) struct Exchanger {
    /// NOTE (lock interdependance): Code taking both `models` and
    /// `most_recent_model_uuid` simultaneously should take `models` first,
    /// to avoid the possibility of a deadlock
    // FIXME: use a concurrent hash map or something better
    // (inference runs can be long, and shouldn't lock the whole map)
    models: std::sync::Arc<Mutex<HashMap<Uuid, InferenceModel>>>,
    most_recent_model_uuid: std::sync::Arc<Mutex<Option<Uuid>>>,
    identity: Arc<MyIdentity>,
    max_model_size: usize,
    max_input_size: usize,
}

impl Exchanger {
    pub fn new(identity: Arc<MyIdentity>, max_model_size: usize, max_input_size: usize) -> Self {
        Self {
            identity,
            models: Arc::new(Mutex::new(HashMap::new())),
            max_model_size,
            max_input_size,
            most_recent_model_uuid: Arc::new(Mutex::new(None)),
        }
    }
}

#[tonic::async_trait]
impl Exchange for Exchanger {
    async fn send_model(
        &self,
        request: Request<tonic::Streaming<SendModelRequest>>,
    ) -> Result<Response<SendModelReply>, Status> {
        let start_time = Instant::now();

        let mut stream = request.into_inner();
        let mut datum = ModelDatumType::I64; // dummy

        let mut input_fact: Vec<usize> = Vec::new();
        let mut model_bytes: Vec<u8> = Vec::new();
        let max_model_size = self.max_model_size;
        let mut model_size = 0usize;
        let mut sign = false;

        let mut model_name = None;
        let mut client_info = None;

        // Get all model chunks from the client into a big Vec
        while let Some(model_stream) = stream.next().await {
            let mut model_proto = model_stream?;
            if model_size == 0 {
                model_size = model_proto.length.try_into().unwrap();
                model_bytes.reserve_exact(model_size);

                model_name = if !model_proto.model_name.is_empty() {
                    Some(model_proto.model_name)
                } else {
                    None
                };
                client_info = model_proto.client_info;

                for x in &model_proto.input_fact {
                    input_fact.push(*x as usize);
                }

                datum = FromPrimitive::from_i32(model_proto.datum)
                    .ok_or_else(|| Status::invalid_argument("Unknown datum type".to_string()))?;
                sign = model_proto.sign;
                model_name = model_proto.model_name.to_string();
            }
            if model_size > max_model_size || model_bytes.len() > max_model_size {
                return Err(Status::invalid_argument("Model too big".to_string()));
            }
            model_bytes.append(&mut model_proto.data)
        }

        if model_size == 0 {
            return Err(Status::invalid_argument("Received no data".to_string()));
        }
        let model_id = Uuid::new_v4();

        // Load the model
        let model = InferenceModel::load_model(
            &model_bytes,
            input_fact.clone(),
            datum,
            model_id,
            model_name,
        )
        .map_err(|err| {
            error!("Unknown error creating model: {}", err);
            Status::unknown("Unknown error".to_string())
        })?;

        // Create an entry in the hashmap
        {
            let mut models = self.models.lock().unwrap();

            // HashMap entry api requires only one lookup and should be prefered than .get()
            // followed with .insert()
            match models.entry(&model_id) {
                Occupied(_) => {
                    error!(
                        "UUID collision: model with uuid ({}) already exists.",
                        model_id
                    );
                    return Err(Status::unknown("Unknown error".to_string()));
                }
                Vacant(entry) => entry.insert(model),
            }
            // `most_recent_model_uuid` is locked here and `models` is not yet unlocked.
            // if some other piece of code locks `models` before `most_recent_model_uuid`,
            // this will result in a probable deadlock situation
            *self.most_recent_model_uuid.lock().unwrap() = Some(model_id);

            // both locks are released here
        }

        // Construct the return payload
        let mut payload = SendModelPayload::default();
        if sign {
            payload.model_hash = digest::digest(&digest::SHA256, &model_bytes)
                .as_ref()
                .to_vec();
            payload.input_fact = input_fact.into_iter().map(|i| i as i32).collect();
        }
        payload.model_id = model_id;
        let payload_with_header = Payload {
            header: Some(PayloadHeader {
                issued_at: Some(SystemTime::now().into()),
            }),
            payload: Some(payload::Payload::SendModelPayload(payload)),
        };

        let mut reply = SendModelReply {
            payload: payload_with_header.encode_to_vec(),
            ..Default::default()
        };
        if sign {
            reply.signature = self
                .identity
                .signing_key
                .sign(&reply.payload)
                .to_bytes()
                .to_vec();
        }

        // Logs and telemetry
        let elapsed = start_time.elapsed();
        info!(
            "[{} {}] SendModel successful in {}ms (model={}, size={}, sign={})",
            client_info
                .as_ref()
                .map(|c| c.user_agent.as_ref())
                .unwrap_or("<unknown>"),
            client_info
                .as_ref()
                .map(|c| c.user_agent_version.as_ref())
                .unwrap_or("<unknown>"),
            elapsed.as_millis(),
            model_name.as_deref().unwrap_or("<unknown>"),
            model_size,
            sign
        );
        telemetry::add_event(
            TelemetryEventProps::SendModel {
                model_size,
                model_name,
                sign,
                time_taken: elapsed.as_secs_f64(),
            },
            client_info,
        );

        Ok(Response::new(reply))
    }

    async fn run_model(
        &self,
        request: Request<tonic::Streaming<RunModelRequest>>,
    ) -> Result<Response<RunModelReply>, Status> {
        let start_time = Instant::now();

        let mut stream = request.into_inner();

        let mut input: Vec<u8> = Vec::new();
        let mut sign = false;
        let max_input_size = self.max_input_size;
        let mut model_id = "".to_string();

        let mut client_info = None;

        // Get all the data and put it in a Vec
        while let Some(data_stream) = stream.next().await {
            let mut data_proto = data_stream?;

            client_info = data_proto.client_info;

            if data_proto.input.len() * size_of::<u8>() > max_input_size
                || input.len() * size_of::<u8>() > max_input_size
            {
                return Err(Status::invalid_argument("Input too big".to_string()));
            }
            if input.is_empty() {
                sign = data_proto.sign;
                model_id = data_proto.model_id;
            }
            input.append(&mut data_proto.input);
        }

        // No model_id was provided, the last insterted model will be used
        if model_id.is_empty() {
            let uuid_guard = self.most_recent_model_uuid.lock().unwrap();
            let id = if let Some(id) = &*uuid_guard {
                id
            } else {
                return Err(Status::invalid_argument(
                    "Cannot find the most recent model",
                ));
            };
            model_id = id.to_string();
        }

        // Find the model with model_id

        {
            let guard = self.models.lock().unwrap();
            let model = match guard.get(&model_id) {
                Some(model) => model,
                None => {
                    return Err(Status::invalid_argument("Model doesn't exist"));
                }
            };

            let result = model.run_inference(&input).map_err(|err| {
                error!("Unknown error running inference: {}", err);
                Status::unknown("Unknown error".to_string())
            })?;
        }

        let mut payload = RunModelPayload {
            output: result,
            ..Default::default()
        };
        if sign {
            payload.input_hash = digest::digest(&digest::SHA256, &input).as_ref().to_vec();
            payload.model_id = model_id;
        }

        let payload_with_header = Payload {
            header: Some(PayloadHeader {
                issued_at: Some(SystemTime::now().into()),
            }),
            payload: Some(payload::Payload::RunModelPayload(payload)),
        };

        let mut reply = RunModelReply {
            payload: payload_with_header.encode_to_vec(),
            ..Default::default()
        };
        if sign {
            reply.signature = self
                .identity
                .signing_key
                .sign(&reply.payload)
                .to_bytes()
                .to_vec();
        }

        // Log and telemetry
        let elapsed = start_time.elapsed();
        info!(
            "[{} {}] RunModel successful in {}ms (model={}, sign={})",
            client_info
                .as_ref()
                .map(|c| c.user_agent.as_ref())
                .unwrap_or("<unknown>"),
            client_info
                .as_ref()
                .map(|c| c.user_agent_version.as_ref())
                .unwrap_or("<unknown>"),
            elapsed.as_millis(),
            model.model_name().unwrap_or("<unknown>"),
            sign
        );
        telemetry::add_event(
            TelemetryEventProps::RunModel {
                model_name: model.model_name().map(|e| e.to_string()),
                sign,
                time_taken: elapsed.as_secs_f64(),
            },
            client_info,
        );

        Ok(Response::new(reply))
    }

    async fn delete_model(
        &self,
        request: Request<DeleteModelRequest>,
    ) -> Result<Response<DeleteModelReply>, Status> {
        let request = request.into_inner();
        let model_id = Uuid::from_str(request.model_id)
            .map_err(|_| Err(Status::invalid_argument("Model doesn't exist")))?;

        // Delete the model
        {
            let mut models = self.models.lock().unwrap();
            if models.remove(&model_id).is_none() {
                error!("Model doesn't exist");
                return Err(Status::invalid_argument("Model doesn't exist"));
            }
        }

        // Construct the payload
        let mut reply = DeleteModelReply {};
        Ok(Response::new(reply))
    }
}
