/*
 *    Copyright (c) 2024 Project CHIP Authors
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#include <algorithm>
#include <app/InternalAttributeOperations.h>

#include <access/SubjectDescriptor.h>
#include <app/AttributeEncodeState.h>
#include <app/AttributeValueDecoder.h>
#include <app/AttributeValueEncoder.h>
#include <app/ConcreteAttributePath.h>
#include <app/InteractionModelEngine.h>
#include <app/MessageDef/ReportDataMessage.h>
#include <app/data-model-provider/OperationTypes.h>
#include <app/data-model-provider/Provider.h>
#include <lib/core/CASEAuthTag.h>
#include <lib/core/CHIPError.h>
#include <lib/core/DataModelTypes.h>
#include <lib/support/BitFlags.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/ScopedBuffer.h>
#include <lib/support/Span.h>
#include <protocols/interaction_model/StatusCode.h>
#include <vector>

using namespace chip;
using namespace chip::app;

namespace {

CHIP_ERROR DecodeAttributeReportIBs(ByteSpan data, std::vector<DecodedAttributeData> & decoded_items)
{
    TLV::TLVReader reportIBsReader;
    reportIBsReader.Init(data);

    ReturnErrorOnFailure(reportIBsReader.Next());
    if (reportIBsReader.GetType() != TLV::TLVType::kTLVType_Structure)
    {
        return CHIP_ERROR_INVALID_ARGUMENT;
    }
    TLV::TLVType outer1;
    reportIBsReader.EnterContainer(outer1);

    ReturnErrorOnFailure(reportIBsReader.Next());
    if (reportIBsReader.GetType() != TLV::TLVType::kTLVType_Array)
    {
        return CHIP_ERROR_INVALID_ARGUMENT;
    }

    TLV::TLVType outer2;
    reportIBsReader.EnterContainer(outer2);

    CHIP_ERROR err = CHIP_NO_ERROR;
    while (CHIP_NO_ERROR == (err = reportIBsReader.Next()))
    {
        TLV::TLVReader attributeReportReader = reportIBsReader;
        AttributeReportIB::Parser attributeReportParser;
        ReturnErrorOnFailure(attributeReportParser.Init(attributeReportReader));

        AttributeDataIB::Parser dataParser;
        ReturnErrorOnFailure(attributeReportParser.GetAttributeData(&dataParser));

        DecodedAttributeData decoded;
        ReturnErrorOnFailure(decoded.DecodeFrom(dataParser));
        decoded_items.push_back(decoded);
    }

    if ((CHIP_END_OF_TLV != err) && (err != CHIP_NO_ERROR))
    {
        return CHIP_NO_ERROR;
    }

    ReturnErrorOnFailure(reportIBsReader.ExitContainer(outer2));
    ReturnErrorOnFailure(reportIBsReader.ExitContainer(outer1));

    err = reportIBsReader.Next();

    if (CHIP_ERROR_END_OF_TLV == err)
    {
        return CHIP_NO_ERROR;
    }
    if (CHIP_NO_ERROR == err)
    {
        return CHIP_ERROR_INVALID_ARGUMENT;
    }

    return err;
}

} // namespace

namespace chip {
namespace app {

CHIP_ERROR DecodedAttributeData::DecodeFrom(const chip::app::AttributeDataIB::Parser & parser)
{
    ReturnErrorOnFailure(parser.GetDataVersion(&dataVersion));
    AttributePathIB::Parser pathParser;
    ReturnErrorOnFailure(parser.GetPath(&pathParser));
    ReturnErrorOnFailure(pathParser.GetConcreteAttributePath(attributePath, AttributePathIB::ValidateIdRanges::kNo));
    ReturnErrorOnFailure(parser.GetData(&dataReader));
    return CHIP_NO_ERROR;
}

std::unique_ptr<AttributeValueEncoder> ReadOperation::StartEncoding(size_t attributeMaxSize)
{
    CHIP_ERROR err = mEncodedIBs.StartEncoding(mAttributeReportIBsBuilder, attributeMaxSize);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DataManagement, "FAILURE starting encoding %" CHIP_ERROR_FORMAT, err.Format());
        return nullptr;
    }

    return std::make_unique<AttributeValueEncoder>(mAttributeReportIBsBuilder, *mRequest.subjectDescriptor, mRequest.path, 0);
}

CHIP_ERROR ReadOperation::FinishEncoding()
{
    return mEncodedIBs.FinishEncoding(mAttributeReportIBsBuilder);
}

Protocols::InteractionModel::Status ReadOperation::GetDecodedAttributeData(size_t maxAttributeSize,
                                                                           DecodedAttributeData & attributeData)
{
    DataModel::Provider * provider = InteractionModelEngine::GetInstance()->GetDataModelProvider();
    VerifyOrReturnValue(provider, Protocols::InteractionModel::Status::Failure);
    std::unique_ptr<chip::app::AttributeValueEncoder> encoder = StartEncoding(maxAttributeSize);
    chip::app::DataModel::ActionReturnStatus status           = provider->ReadAttribute(GetRequest(), *encoder);
    VerifyOrReturnValue(status.IsSuccess(), status.GetStatusCode().GetStatus());
    CHIP_ERROR err = FinishEncoding();
    VerifyOrReturnValue(err == CHIP_NO_ERROR, Protocols::InteractionModel::ClusterStatusCode(err).GetStatus());
    std::vector<DecodedAttributeData> attributeDatas;
    err = GetEncodedIBs().Decode(attributeDatas);
    VerifyOrReturnValue(err == CHIP_NO_ERROR, Protocols::InteractionModel::ClusterStatusCode(err).GetStatus());
    attributeData = attributeDatas[0];
    return Protocols::InteractionModel::Status::Success;
}

CHIP_ERROR EncodedReportIBs::StartEncoding(app::AttributeReportIBs::Builder & builder, size_t attributeMaxSize)
{
    // The TLV data will include an Anonymous struct(2), an AttributeReportIBs list(3), an Anonymous struct(2),
    // an AttributeData struct(3), a DataVersion(max6), an AttributePath list(3) with one attribute Path(EndpointId(4),
    // ClusterId(6), AttributeId(6)) and AttributeData value(max4 + attributeMaxSize). So allocate (attributeMaxSize + 50)
    // bytes for the TLV data buffer.
    mTlvDataBuffer.Alloc(attributeMaxSize + 40);
    VerifyOrReturnError(mTlvDataBuffer.Get(), CHIP_ERROR_NO_MEMORY);
    mEncodeWriter.Init(mTlvDataBuffer.Get(), attributeMaxSize + 40);
    ReturnErrorOnFailure(mEncodeWriter.StartContainer(TLV::AnonymousTag(), TLV::kTLVType_Structure, mOuterStructureType));
    return builder.Init(&mEncodeWriter, to_underlying(ReportDataMessage::Tag::kAttributeReportIBs));
}

CHIP_ERROR EncodedReportIBs::FinishEncoding(app::AttributeReportIBs::Builder & builder)
{
    VerifyOrReturnError(mTlvDataBuffer.Get(), CHIP_ERROR_INCORRECT_STATE);
    builder.EndOfContainer();
    ReturnErrorOnFailure(mEncodeWriter.EndContainer(mOuterStructureType));
    ReturnErrorOnFailure(mEncodeWriter.Finalize());

    mDecodeSpan = ByteSpan(mTlvDataBuffer.Get(), mEncodeWriter.GetLengthWritten());
    return CHIP_NO_ERROR;
}

CHIP_ERROR EncodedReportIBs::Decode(std::vector<DecodedAttributeData> & decoded_items) const
{
    return DecodeAttributeReportIBs(mDecodeSpan, decoded_items);
}

Protocols::InteractionModel::Status InternalReadAttribute(const ConcreteAttributePath & path, MutableCharSpan & value,
                                                          size_t maxLen)
{
    ReadOperation request(path);
    DecodedAttributeData attributeData;
    Protocols::InteractionModel::Status ret = request.GetDecodedAttributeData(maxLen, attributeData);
    VerifyOrReturnValue(ret == Protocols::InteractionModel::Status::Success, ret);
    CharSpan read;
    CHIP_ERROR err = DataModel::Decode(attributeData.dataReader, read);
    VerifyOrReturnValue(err == CHIP_NO_ERROR, Protocols::InteractionModel::ClusterStatusCode(err).GetStatus());
    err = CopyCharSpanToMutableCharSpan(read, value);
    VerifyOrReturnValue(err == CHIP_NO_ERROR, Protocols::InteractionModel::ClusterStatusCode(err).GetStatus());
    return Protocols::InteractionModel::Status::Success;
}

Protocols::InteractionModel::Status InternalReadAttribute(const ConcreteAttributePath & path,
                                                          DataModel::Nullable<MutableCharSpan> & value, size_t maxLen)
{
    if (value.IsNull())
    {
        return Protocols::InteractionModel::Status::Failure;
    }
    ReadOperation request(path);
    DecodedAttributeData attributeData;
    Protocols::InteractionModel::Status ret = request.GetDecodedAttributeData(maxLen, attributeData);
    VerifyOrReturnValue(ret == Protocols::InteractionModel::Status::Success, ret);
    DataModel::Nullable<CharSpan> read;
    CHIP_ERROR err = DataModel::Decode(attributeData.dataReader, read);
    VerifyOrReturnValue(err == CHIP_NO_ERROR, Protocols::InteractionModel::ClusterStatusCode(err).GetStatus());
    if (read.IsNull())
    {
        value.SetNull();
        return Protocols::InteractionModel::Status::Success;
    }
    err = CopyCharSpanToMutableCharSpan(read.Value(), value.Value());
    VerifyOrReturnValue(err == CHIP_NO_ERROR, Protocols::InteractionModel::ClusterStatusCode(err).GetStatus());
    return Protocols::InteractionModel::Status::Success;
}

Protocols::InteractionModel::Status InternalReadAttribute(const ConcreteAttributePath & path, MutableByteSpan & value,
                                                          size_t maxLen)
{
    ReadOperation request(path);
    DecodedAttributeData attributeData;
    Protocols::InteractionModel::Status ret = request.GetDecodedAttributeData(maxLen, attributeData);
    VerifyOrReturnValue(ret == Protocols::InteractionModel::Status::Success, ret);
    ByteSpan read;
    CHIP_ERROR err = DataModel::Decode(attributeData.dataReader, read);
    VerifyOrReturnValue(err == CHIP_NO_ERROR, Protocols::InteractionModel::ClusterStatusCode(err).GetStatus());
    err = CopySpanToMutableSpan(read, value);
    VerifyOrReturnValue(err == CHIP_NO_ERROR, Protocols::InteractionModel::ClusterStatusCode(err).GetStatus());
    return Protocols::InteractionModel::Status::Success;
}

Protocols::InteractionModel::Status InternalReadAttribute(const ConcreteAttributePath & path,
                                                          DataModel::Nullable<MutableByteSpan> & value, size_t maxLen)
{
    if (value.IsNull())
    {
        return Protocols::InteractionModel::Status::Failure;
    }
    ReadOperation request(path);
    DecodedAttributeData attributeData;
    Protocols::InteractionModel::Status ret = request.GetDecodedAttributeData(maxLen, attributeData);
    VerifyOrReturnValue(ret == Protocols::InteractionModel::Status::Success, ret);
    DataModel::Nullable<ByteSpan> read;
    CHIP_ERROR err = DataModel::Decode(attributeData.dataReader, read);
    VerifyOrReturnValue(err == CHIP_NO_ERROR, Protocols::InteractionModel::ClusterStatusCode(err).GetStatus());
    if (read.IsNull())
    {
        value.SetNull();
        return Protocols::InteractionModel::Status::Success;
    }
    err = CopySpanToMutableSpan(read.Value(), value.Value());
    VerifyOrReturnValue(err == CHIP_NO_ERROR, Protocols::InteractionModel::ClusterStatusCode(err).GetStatus());
    return Protocols::InteractionModel::Status::Success;
}

Protocols::InteractionModel::Status InternalWriteAttribute(const ConcreteAttributePath & path, const CharSpan & x, size_t maxLen,
                                                           std::optional<bool> markDirty)
{
    WriteOperation request(path);
    AttributeValueDecoder decoder = request.DecoderFor(x, maxLen);
    return request.WriteEncodedAttribute(decoder, markDirty);
}

Protocols::InteractionModel::Status InternalWriteAttribute(const ConcreteAttributePath & path, const ByteSpan & x, size_t maxLen,
                                                           std::optional<bool> markDirty)
{
    WriteOperation request(path);
    AttributeValueDecoder decoder = request.DecoderFor(x, maxLen);
    return request.WriteEncodedAttribute(decoder, markDirty);
}

Protocols::InteractionModel::Status InternalWriteAttributeNull(const ConcreteAttributePath & path, std::optional<bool> markDirty)
{
    WriteOperation request(path);
    AttributeValueDecoder decoder = request.DecoderForNull();
    return request.WriteEncodedAttribute(decoder, markDirty);
}

} // namespace app
} // namespace chip
