# Copyright 2022 Mithril Security. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import os
import logging
import ssl
from enum import IntEnum
from cbor2 import dumps
from socket import setdefaulttimeout
from grpc import ssl_channel_credentials, secure_channel, RpcError
from utils.utils import create_byte_chunk, encode_certificate, strip_https
from utils.errors import Actions, check_exception, check_rpc_exception

from dcap_attestation import (
    verify_claims,
    verify_dcap_attestation,
    get_server_cert,
    load_policy,
)

# These modules are generated by grpc proto compiler, from proto files in proto
from securedexchange_pb2 import SimpleReply, ModelResult, Model, Data
from securedexchange_pb2_grpc import ExchangeStub
from untrusted_pb2_grpc import AttestationStub
from untrusted_pb2 import (
    GetCertificateRequest as certificate_request,
    GetSgxQuoteWithCollateralRequest as quote_request,
)

PORTS = {"untrusted_enclave": "50052", "attested_enclave": "50051"}
TIMEOUT = 10


class ModelDatumType(IntEnum):
    F32 = 0
    F64 = 1
    I32 = 2
    I64 = 3
    U32 = 4
    U64 = 5


class BlindAiClient:
    def __init__(self, debug_mode=False):

        self.channel = None
        self.policy = None
        self.stub = None
        self.DEBUG_MODE = debug_mode

        if debug_mode == True:
            os.environ["GRPC_TRACE"] = "transport_security,tsi"
            os.environ["GRPC_VERBOSITY"] = "DEBUG"
        self.SIMULATION_MODE = False

    def _is_connected(self):
        return self.channel is not None

    def _close_channel(self):
        if self._is_connected():
            self.channel.close()

    def connect_server(
        self,
        addr: str,
        server_name="blindai-srv",
        policy=None,
        certificate=None,
        simulation=False,
    ):
        """Connect to the server with the specified parameters.
        You will have to specify here the expected policy (server identity, configuration...) and the server TLS certificate, if you are using the hardware mode.

        If you're using the simulation mode, you don't need to provide a policy and certificate, but please keep in mind that
        this mode should NEVER be used in production as it doesn't have most of the security provided by the hardware mode.

        Args:
            addr: The address of BlindAI server you want to reach.
            server_name: Contains the CN expected by the server TLS certificate.
            policy: Path to the toml file describing the policy of the server. Generated in the server side.
            certificate: Path to the public key of the untrusted inference server. Generated in the server side.
            simulation:  Connect to the server in simulation mode (default False). If set to yes, the args policy and certificate will be ignored.

        Raises:
            ValueError: Will be raised in case the policy doesn't match the server identity and configuration.
            ConnectionError: Will be raised in case the connection with the server fails.
            IOError: Will be raised in case reading certificate, policy or model files fails.
        """
        self.SIMULATION_MODE = simulation
        self.DISABLE_UNTRUSTED_SERVER_CERT_CHECK = simulation

        error = None
        action = None

        addr = strip_https(addr)
        untrusted_client_to_enclave = addr + ":" + PORTS["untrusted_enclave"]
        attested_client_to_enclave = addr + ":" + PORTS["attested_enclave"]
        try:
            if self.DISABLE_UNTRUSTED_SERVER_CERT_CHECK:
                logging.warning("Untrusted server certificate check bypassed")
                action = Actions.GET_UNTRUSTED_SERVER_CERT
                setdefaulttimeout(TIMEOUT)
                untrusted_server_cert = ssl.get_server_certificate(
                    (addr, PORTS["untrusted_enclave"])
                )
                untrusted_server_creds = ssl_channel_credentials(
                    root_certificates=bytes(untrusted_server_cert, encoding="utf8")
                )
            else:
                action = Actions.READ_CERT_FILE
                with open(certificate, "rb") as f:
                    action = Actions.CONNECT_SERVER
                    untrusted_server_creds = ssl_channel_credentials(
                        root_certificates=f.read()
                    )

            connection_options = (("grpc.ssl_target_name_override", server_name),)

            action = Actions.CONNECT_SERVER
            channel = secure_channel(
                untrusted_client_to_enclave,
                untrusted_server_creds,
                options=connection_options,
            )
            stub = AttestationStub(channel)
            if self.SIMULATION_MODE:
                logging.warning(
                    "Attestation process is bypassed : running without requesting and checking attestation"
                )
                response = stub.GetCertificate(certificate_request())
                server_cert = encode_certificate(response.enclave_tls_certificate)

            else:
                action = Actions.LOAD_POLICY
                self.policy = load_policy(policy)

                action = Actions.CONNECT_SERVER
                response = stub.GetSgxQuoteWithCollateral(quote_request())
                claims = verify_dcap_attestation(
                    response.quote, response.collateral, response.enclave_held_data
                )

                action = Actions.VERIFY_CLAIMS
                verify_claims(claims, self.policy)
                server_cert = get_server_cert(claims)

                logging.info(f"Quote verification passed")
                logging.info(
                    f"Certificate from attestation process\n {server_cert.decode('ascii')}"
                )
                logging.info(f"MREnclave\n" + claims["sgx-mrenclave"])

            channel.close()

            action = Actions.CONNECT_SERVER
            server_creds = ssl_channel_credentials(root_certificates=server_cert)
            channel = secure_channel(
                attested_client_to_enclave, server_creds, options=connection_options
            )

            self.stub = ExchangeStub(channel)
            self.channel = channel
            logging.info("Successfuly connected to the server")

        except RpcError as rpc_err:
            error = check_rpc_exception(
                rpc_err, action, self.SIMULATION_MODE, self.DEBUG_MODE
            )

        except IOError as io_err:
            if action == Actions.LOAD_POLICY:
                action = Actions.READ_POLICY_FILE
            error = check_exception(
                io_err, action, self.SIMULATION_MODE, self.DEBUG_MODE
            )

        except Exception as err:
            error = error = check_exception(
                err, action, self.SIMULATION_MODE, self.DEBUG_MODE
            )

        finally:
            if error:
                raise error

    def upload_model(self, model=None, shape=None, dtype=ModelDatumType.F32):
        """Upload an inference model to the server.
        The provided model needs to be in the Onnx format.

        Args:
            model: Path to Onnx model file.
            shape: The shape of the model input.
            dtype: The type of the model input data (f32 by default)

        Returns:
            SimpleReply object, containing two fields:
                ok:  Set to True if model was loaded successfully, False otherwise
                msg: Error message if any.
        """

        response = SimpleReply()
        response.ok = False
        if dtype is None:
            dtype = ModelDatumType.F32
        if not self._is_connected():
            response.msg = "Not connected to server"
            return response

        try:
            with open(model, "rb") as f:
                data = f.read()
            input_fact = list(shape)
            response = self.stub.SendModel(
                iter(
                    [
                        Model(
                            length=len(data),
                            input_fact=input_fact,
                            data=chunk,
                            datum=int(dtype),
                        )
                        for chunk in create_byte_chunk(data)
                    ]
                )
            )

        except RpcError as rpc_error:
            response.msg = "GRPC error"

        except FileNotFoundError:
            response.msg = "Model not found"

        return response

    def run_model(self, data_list):
        """Send data to the server to make a secure inference

        The data provided must be in a list, as the tensor will be rebuilt inside the server.

        Args:
            data_list: array of numbers, the numbers must be of the same type dtype specified in upload_model

        Returns:
            ModelResult object, containing three fields:
                output: array of floats. The inference results returned by the model.
                ok:  Set to True if the inference was run successfully, False otherwise
                msg: Error message if any.
        """

        response = ModelResult()
        response.ok = False
        if not self._is_connected():
            response.msg = "Not connected to server"
            return response

        try:
            serialized_bytes = dumps(data_list)
            response = self.stub.RunModel(
                iter(
                    [
                        Data(input=serialized_bytes_chunk)
                        for serialized_bytes_chunk in create_byte_chunk(
                            serialized_bytes
                        )
                    ]
                )
            )
            return response

        except RpcError as rpc_error:
            response.msg = "GRPC error"

        return response

    def close_connection(self):
        """Close the connection between the client and the inference server."""
        if self._is_connected():
            self._close_channel()
            self.channel = None
            self.stub = None
            self.policy = None
