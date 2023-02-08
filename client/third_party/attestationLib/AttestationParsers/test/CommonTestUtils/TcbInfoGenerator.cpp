/*
 * Copyright (C) 2011-2021 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "TcbInfoGenerator.h"

#include <cstring>

namespace intel { namespace sgx { namespace dcap {

const std::string validSgxTcb = R"json(
    "tcb": {
        "sgxtcbcomp01svn": 12,
        "sgxtcbcomp02svn": 23,
        "sgxtcbcomp03svn": 34,
        "sgxtcbcomp04svn": 45,
        "sgxtcbcomp05svn": 100,
        "sgxtcbcomp06svn": 0,
        "sgxtcbcomp07svn": 1,
        "sgxtcbcomp08svn": 156,
        "sgxtcbcomp09svn": 208,
        "sgxtcbcomp10svn": 255,
        "sgxtcbcomp11svn": 2,
        "sgxtcbcomp12svn": 3,
        "sgxtcbcomp13svn": 4,
        "sgxtcbcomp14svn": 5,
        "sgxtcbcomp15svn": 6,
        "sgxtcbcomp16svn": 7,
        "pcesvn": 30865
    })json";

const std::string validUpToDateStatus = R"json("status": "UpToDate")json";
const std::string validOutOfDateStatus = R"json("status": "OutOfDate")json";
const std::string validRevokedStatus = R"json("status": "Revoked")json";
const std::string validConfigurationNeededStatus = R"json("status": "ConfigurationNeeded")json";
const std::string validTcbLevelV1Template = "{%s, %s}";
const std::string validTcbLevelV2Template = R"json({%s, %s, %s, %s})json";

const std::string validTcbInfoV1Template = R"json({
        "tcbInfo": {
            "version": 1,
            "issueDate": "2017-10-04T11:10:45Z",
            "nextUpdate": "2018-06-21T12:36:02Z",
            "fmspc": "0192837465AF",
            "pceId": "0000",
            "tcbLevels": [%s]
        },
        %s})json";

const std::string validTcbInfoV2Template = R"json({
        "tcbInfo": {
            "version": 2,
            "issueDate": "2017-10-04T11:10:45Z",
            "nextUpdate": "2018-06-21T12:36:02Z",
            "fmspc": "0192837465AF",
            "pceId": "0000",
            "tcbType": 1,
            "tcbEvaluationDataNumber": 1,
            "tcbLevels": [%s]
        },
        %s})json";

const std::string validSignatureTemplate = R"json("signature": "62f2eb97227d906c158e8500964c8d10029e1a318e0e95054fbc1b9636913555d7147ceefe07c4cb7ac1ac700093e2ee3fd4f7d00c7caf135dc5243be51e1def")json";

const int DEFAULT_TCB_TYPE = 1;
const int DEFAULT_TCB_EVALUATION_DATA_NUMBER = 1;
const std::vector<uint8_t> DEFAULT_CPUSVN {12, 23, 34, 45, 100, 0, 1, 156, 208, 255, 2, 3, 4, 5, 6, 7};
const int DEFAULT_PCESVN = 30865;
const std::vector<uint8_t> DEFAULT_FMSPC = { 0x01, 0x92, 0x83, 0x74, 0x65, 0xAF };
const std::vector<uint8_t> DEFAULT_PCEID = { 0x00, 0x00 };
const std::vector<uint8_t> DEFAULT_SIGNATURE = { 0x62, 0xF2, 0xEB, 0x97, 0x22, 0x7D, 0x90, 0x6C,
												 0x15, 0x8E, 0x85, 0x00, 0x96, 0x4C, 0x8D, 0x10,
												 0x02, 0x9E, 0x1A, 0x31, 0x8E, 0x0E, 0x95, 0x05,
												 0x4F, 0xBC, 0x1B, 0x96, 0x36, 0x91, 0x35, 0x55,
												 0xD7, 0x14, 0x7C, 0xEE, 0xFE, 0x07, 0xC4, 0xCB,
												 0x7A, 0xC1, 0xAC, 0x70, 0x00, 0x93, 0xE2, 0xEE,
												 0x3F, 0xD4, 0xF7, 0xD0, 0x0C, 0x7C, 0xAF, 0x13,
												 0x5D, 0xC5, 0x24, 0x3B, 0xE5, 0x1E, 0x1D, 0xEF };
const std::vector<uint8_t> DEFAULT_INFO_BODY = { 0x7B, 0x22, 0x76, 0x65, 0x72, 0x73, 0x69, 0x6F,
												 0x6E, 0x22, 0x3A, 0x31, 0x2C, 0x22, 0x69, 0x73,
												 0x73, 0x75, 0x65, 0x44, 0x61, 0x74, 0x65, 0x22,
												 0x3A, 0x22, 0x32, 0x30, 0x31, 0x37, 0x2D, 0x31,
												 0x30, 0x2D, 0x30, 0x34, 0x54, 0x31, 0x31, 0x3A,
												 0x31, 0x30, 0x3A, 0x34, 0x35, 0x5A, 0x22, 0x2C,
												 0x22, 0x6E, 0x65, 0x78, 0x74, 0x55, 0x70, 0x64,
												 0x61, 0x74, 0x65, 0x22, 0x3A, 0x22, 0x32, 0x30,
												 0x31, 0x38, 0x2D, 0x30, 0x36, 0x2D, 0x32, 0x31,
												 0x54, 0x31, 0x32, 0x3A, 0x33, 0x36, 0x3A, 0x30,
												 0x32, 0x5A, 0x22, 0x2C, 0x22, 0x66, 0x6D, 0x73,
												 0x70, 0x63, 0x22, 0x3A, 0x22, 0x30, 0x31, 0x39,
												 0x32, 0x38, 0x33, 0x37, 0x34, 0x36, 0x35, 0x41,
												 0x46, 0x22, 0x2C, 0x22, 0x70, 0x63, 0x65, 0x49,
												 0x64, 0x22, 0x3A, 0x22, 0x30, 0x30, 0x30, 0x30,
												 0x22, 0x2C, 0x22, 0x74, 0x63, 0x62, 0x4C, 0x65,
												 0x76, 0x65, 0x6C, 0x73, 0x22, 0x3A, 0x5B, 0x7B,
												 0x22, 0x74, 0x63, 0x62, 0x22, 0x3A, 0x7B, 0x22,
												 0x73, 0x67, 0x78, 0x74, 0x63, 0x62, 0x63, 0x6F,
												 0x6D, 0x70, 0x30, 0x31, 0x73, 0x76, 0x6E, 0x22,
												 0x3A, 0x31, 0x32, 0x2C, 0x22, 0x73, 0x67, 0x78,
												 0x74, 0x63, 0x62, 0x63, 0x6F, 0x6D, 0x70, 0x30,
												 0x32, 0x73, 0x76, 0x6E, 0x22, 0x3A, 0x32, 0x33,
												 0x2C, 0x22, 0x73, 0x67, 0x78, 0x74, 0x63, 0x62,
												 0x63, 0x6F, 0x6D, 0x70, 0x30, 0x33, 0x73, 0x76,
												 0x6E, 0x22, 0x3A, 0x33, 0x34, 0x2C, 0x22, 0x73,
												 0x67, 0x78, 0x74, 0x63, 0x62, 0x63, 0x6F, 0x6D,
												 0x70, 0x30, 0x34, 0x73, 0x76, 0x6E, 0x22, 0x3A,
												 0x34, 0x35, 0x2C, 0x22, 0x73, 0x67, 0x78, 0x74,
												 0x63, 0x62, 0x63, 0x6F, 0x6D, 0x70, 0x30, 0x35,
												 0x73, 0x76, 0x6E, 0x22, 0x3A, 0x31, 0x30, 0x30,
												 0x2C, 0x22, 0x73, 0x67, 0x78, 0x74, 0x63, 0x62,
												 0x63, 0x6F, 0x6D, 0x70, 0x30, 0x36, 0x73, 0x76,
												 0x6E, 0x22, 0x3A, 0x30, 0x2C, 0x22, 0x73, 0x67,
												 0x78, 0x74, 0x63, 0x62, 0x63, 0x6F, 0x6D, 0x70,
												 0x30, 0x37, 0x73, 0x76, 0x6E, 0x22, 0x3A, 0x31,
												 0x2C, 0x22, 0x73, 0x67, 0x78, 0x74, 0x63, 0x62,
												 0x63, 0x6F, 0x6D, 0x70, 0x30, 0x38, 0x73, 0x76,
												 0x6E, 0x22, 0x3A, 0x31, 0x35, 0x36, 0x2C, 0x22,
												 0x73, 0x67, 0x78, 0x74, 0x63, 0x62, 0x63, 0x6F,
												 0x6D, 0x70, 0x30, 0x39, 0x73, 0x76, 0x6E, 0x22,
												 0x3A, 0x32, 0x30, 0x38, 0x2C, 0x22, 0x73, 0x67,
												 0x78, 0x74, 0x63, 0x62, 0x63, 0x6F, 0x6D, 0x70,
												 0x31, 0x30, 0x73, 0x76, 0x6E, 0x22, 0x3A, 0x32,
												 0x35, 0x35, 0x2C, 0x22, 0x73, 0x67, 0x78, 0x74,
												 0x63, 0x62, 0x63, 0x6F, 0x6D, 0x70, 0x31, 0x31,
												 0x73, 0x76, 0x6E, 0x22, 0x3A, 0x32, 0x2C, 0x22,
												 0x73, 0x67, 0x78, 0x74, 0x63, 0x62, 0x63, 0x6F,
												 0x6D, 0x70, 0x31, 0x32, 0x73, 0x76, 0x6E, 0x22,
												 0x3A, 0x33, 0x2C, 0x22, 0x73, 0x67, 0x78, 0x74,
												 0x63, 0x62, 0x63, 0x6F, 0x6D, 0x70, 0x31, 0x33,
												 0x73, 0x76, 0x6E, 0x22, 0x3A, 0x34, 0x2C, 0x22,
												 0x73, 0x67, 0x78, 0x74, 0x63, 0x62, 0x63, 0x6F,
												 0x6D, 0x70, 0x31, 0x34, 0x73, 0x76, 0x6E, 0x22,
												 0x3A, 0x35, 0x2C, 0x22, 0x73, 0x67, 0x78, 0x74,
												 0x63, 0x62, 0x63, 0x6F, 0x6D, 0x70, 0x31, 0x35,
												 0x73, 0x76, 0x6E, 0x22, 0x3A, 0x36, 0x2C, 0x22,
												 0x73, 0x67, 0x78, 0x74, 0x63, 0x62, 0x63, 0x6F,
												 0x6D, 0x70, 0x31, 0x36, 0x73, 0x76, 0x6E, 0x22,
												 0x3A, 0x37, 0x2C, 0x22, 0x70, 0x63, 0x65, 0x73,
												 0x76, 0x6E, 0x22, 0x3A, 0x33, 0x30, 0x38, 0x36,
												 0x35, 0x7D, 0x2C, 0x22, 0x73, 0x74, 0x61, 0x74,
												 0x75, 0x73, 0x22, 0x3A, 0x22, 0x55, 0x70, 0x54,
												 0x6F, 0x44, 0x61, 0x74, 0x65, 0x22, 0x7D, 0x5D,
												 0x7D };
const std::string DEFAULT_ISSUE_DATE = "2017-10-04T11:10:45Z";
const std::string DEFAULT_NEXT_UPDATE = "2018-06-21T12:36:02Z";
const std::string  DEFAULT_TCB_DATE = "2019-05-23T10:36:02Z";

std::string TcbInfoGenerator::generateTcbInfo(const std::string &tcbInfoTemplate,
											  const std::string &tcbLevelsJson,
											  const std::string &signature)
{
	auto jsonSize = tcbInfoTemplate.length() + tcbLevelsJson.length() + signature.length() + 1;
	std::string tcbInfo;
	tcbInfo.resize(jsonSize);
	snprintf(&tcbInfo[0], jsonSize, tcbInfoTemplate.c_str(), tcbLevelsJson.c_str(), signature.c_str());
	return tcbInfo;
}

std::string TcbInfoGenerator::generateTcbLevelV1(const std::string &tcbLevelTemplate,
                                                 const std::string &tcb,
                                                 const std::string &status)
{
	auto jsonSize = tcbLevelTemplate.length() + tcb.length() + status.length() + 1;
	std::string tcbInfo;
	tcbInfo.resize(jsonSize);
	snprintf(&tcbInfo[0], jsonSize, tcbLevelTemplate.c_str(), tcb.c_str(), status.c_str());
	tcbInfo.resize(tcbInfo.find('\000')); // Remove extra characters ('\000')
	return tcbInfo;
}

std::string TcbInfoGenerator::generateTcbLevelV2(const std::string &tcbLevelTemplate,
                                                 const std::string &tcb,
                                                 const std::string &status,
                                                 const std::string &tcbDate,
                                                 const std::string &advisoryIDs)
{
    auto jsonSize = tcbLevelTemplate.length() + tcb.length() + status.length() + tcbDate.length() + advisoryIDs.length() + 1;
    std::string tcbInfo;
    tcbInfo.resize(jsonSize);
    snprintf(&tcbInfo[0], jsonSize, tcbLevelTemplate.c_str(), tcb.c_str(), status.c_str(), tcbDate.c_str(), advisoryIDs.c_str());
    return tcbInfo;
}

}}}