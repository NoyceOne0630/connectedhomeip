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

#pragma once

#include <access/SubjectDescriptor.h>
#include <app/AttributeValueDecoder.h>
#include <app/AttributeValueEncoder.h>
#include <app/ConcreteAttributePath.h>
#include <app/InteractionModelEngine.h>
#include <app/MessageDef/AttributeDataIB.h>
#include <app/data-model-provider/OperationTypes.h>
#include <app/data-model-provider/Provider.h>
#include <app/data-model/Nullable.h>
#include <lib/support/Span.h>
#include <optional>
#include <protocols/interaction_model/StatusCode.h>
#include <vector>

namespace chip {
namespace app {

struct DecodedAttributeData
{
    chip::DataVersion dataVersion;
    chip::app::ConcreteDataAttributePath attributePath;
    chip::TLV::TLVReader dataReader;

    CHIP_ERROR DecodeFrom(const chip::app::AttributeDataIB::Parser & parser);
};

constexpr Access::SubjectDescriptor kInternalOperationSubjectDescriptor{
    .fabricIndex = kMinValidFabricIndex,
    .authMode    = Access::AuthMode::kCase,
    .subject     = kUndefinedNodeId,
    .cats        = kUndefinedCATs,
};

class EncodedReportIBs
{
public:
    CHIP_ERROR StartEncoding(app::AttributeReportIBs::Builder & builder, size_t maxAttributeSize);
    CHIP_ERROR FinishEncoding(app::AttributeReportIBs::Builder & builder);

    CHIP_ERROR Decode(std::vector<DecodedAttributeData> & decoded_items) const;

private:
    Platform::ScopedMemoryBuffer<uint8_t> mTlvDataBuffer;
    TLV::TLVType mOuterStructureType;
    TLV::TLVWriter mEncodeWriter;
    ByteSpan mDecodeSpan;
};

class ReadOperation
{
public:
    ReadOperation(const ConcreteAttributePath & path)
    {
        mRequest.path              = path;
        mRequest.subjectDescriptor = &kInternalOperationSubjectDescriptor;
        mRequest.operationFlags.Set(DataModel::OperationFlags::kInternal);
    }

    ReadOperation(EndpointId endpoint, ClusterId cluster, AttributeId attribute) :
        ReadOperation(ConcreteAttributePath(endpoint, cluster, attribute))
    {}

    Protocols::InteractionModel::Status GetDecodedAttributeData(size_t maxAttributeSize, DecodedAttributeData & attributeData);

    const DataModel::ReadAttributeRequest & GetRequest() const { return mRequest; }

    const EncodedReportIBs & GetEncodedIBs() const { return mEncodedIBs; }

private:
    std::unique_ptr<AttributeValueEncoder> StartEncoding(size_t maxAttributeSize);
    CHIP_ERROR FinishEncoding();

    DataModel::ReadAttributeRequest mRequest;
    // encoded-used classes
    EncodedReportIBs mEncodedIBs;
    AttributeReportIBs::Builder mAttributeReportIBsBuilder;
};

class WriteOperation
{
public:
    WriteOperation(const ConcreteDataAttributePath & path)
    {
        mRequest.path              = path;
        mRequest.subjectDescriptor = &kInternalOperationSubjectDescriptor;
        mRequest.operationFlags.Set(DataModel::OperationFlags::kInternal);
    }

    WriteOperation(EndpointId endpoint, ClusterId cluster, AttributeId attribute) :
        WriteOperation(ConcreteDataAttributePath(endpoint, cluster, attribute))
    {}

    WriteOperation(const ConcreteAttributePath & path) : WriteOperation(ConcreteDataAttributePath(path)) {}

    const DataModel::WriteAttributeRequest & GetRequest() const { return mRequest; }
    Protocols::InteractionModel::Status WriteEncodedAttribute(AttributeValueDecoder & decoder,
                                                              std::optional<bool> markDirty = std::nullopt)
    {
        auto * engine = InteractionModelEngine::GetInstance();
        VerifyOrDie(engine);
        DataModel::Provider * provider = engine->GetDataModelProvider();
        VerifyOrDie(provider);
        DataModel::ActionReturnStatus status = provider->WriteAttribute(GetRequest(), decoder, markDirty);
        return status.GetStatusCode().GetStatus();
    }

    template <class T>
    AttributeValueDecoder DecoderFor(const T & value, size_t maxAttributeSize)
    {
        mTLVBuffer.Alloc(maxAttributeSize + 50);
        VerifyOrDie(mTLVBuffer.Get());
        mTLVReader = ReadEncodedValue(value);
        return AttributeValueDecoder(mTLVReader, *mRequest.subjectDescriptor);
    }

    AttributeValueDecoder DecoderForNull()
    {
        mTLVBuffer.Alloc(50);
        VerifyOrDie(mTLVBuffer.Get());
        mTLVReader = ReadNullEncodedValue();
        return AttributeValueDecoder(mTLVReader, *mRequest.subjectDescriptor);
    }

private:
    template <typename T>
    TLV::TLVReader ReadEncodedValue(const T & value)
    {
        TLV::TLVWriter writer;
        writer.Init(mTLVBuffer.Get(), mTLVBuffer.AllocatedSize());
        TLV::TLVType outerContainerType;
        VerifyOrDie(writer.StartContainer(TLV::AnonymousTag(), TLV::kTLVType_Structure, outerContainerType) == CHIP_NO_ERROR);
        VerifyOrDie(chip::app::DataModel::Encode(writer, TLV::ContextTag(1), value) == CHIP_NO_ERROR);
        VerifyOrDie(writer.EndContainer(outerContainerType) == CHIP_NO_ERROR);
        VerifyOrDie(writer.Finalize() == CHIP_NO_ERROR);
        TLV::TLVReader reader;
        reader.Init(mTLVBuffer.Get(), mTLVBuffer.AllocatedSize());
        VerifyOrDie(reader.Next() == CHIP_NO_ERROR);
        VerifyOrDie(reader.EnterContainer(outerContainerType) == CHIP_NO_ERROR);
        VerifyOrDie(reader.Next() == CHIP_NO_ERROR);
        return reader;
    }

    TLV::TLVReader ReadNullEncodedValue()
    {
        TLV::TLVWriter writer;
        writer.Init(mTLVBuffer.Get(), mTLVBuffer.AllocatedSize());
        TLV::TLVType outerContainerType;
        VerifyOrDie(writer.StartContainer(TLV::AnonymousTag(), TLV::kTLVType_Structure, outerContainerType) == CHIP_NO_ERROR);
        VerifyOrDie(writer.PutNull(TLV::ContextTag(1)) == CHIP_NO_ERROR);
        VerifyOrDie(writer.EndContainer(outerContainerType) == CHIP_NO_ERROR);
        VerifyOrDie(writer.Finalize() == CHIP_NO_ERROR);
        TLV::TLVReader reader;
        reader.Init(mTLVBuffer.Get(), mTLVBuffer.AllocatedSize());
        VerifyOrDie(reader.Next() == CHIP_NO_ERROR);
        VerifyOrDie(reader.EnterContainer(outerContainerType) == CHIP_NO_ERROR);
        VerifyOrDie(reader.Next() == CHIP_NO_ERROR);
        return reader;
    }

    DataModel::WriteAttributeRequest mRequest;
    // where data is being written
    Platform::ScopedMemoryBufferWithSize<uint8_t> mTLVBuffer;

    // tlv reader used for the returned AttributeValueDecoder (since attributeValueDecoder uses references)
    TLV::TLVReader mTLVReader;
};

template <typename X>
Protocols::InteractionModel::Status InternalReadAttribute(const ConcreteAttributePath & path, X & x)
{
    ReadOperation request(path);
    DecodedAttributeData attributeData;
    Protocols::InteractionModel::Status ret = request.GetDecodedAttributeData(sizeof(X), attributeData);
    VerifyOrReturnValue(ret == Protocols::InteractionModel::Status::Success, ret);
    CHIP_ERROR err = DataModel::Decode(attributeData.dataReader, x);
    VerifyOrReturnValue(err == CHIP_NO_ERROR, Protocols::InteractionModel::ClusterStatusCode(err).GetStatus());
    return Protocols::InteractionModel::Status::Success;
}

Protocols::InteractionModel::Status InternalReadAttribute(const ConcreteAttributePath & path, MutableCharSpan & value,
                                                          size_t maxLen);

Protocols::InteractionModel::Status InternalReadAttribute(const ConcreteAttributePath & path,
                                                          DataModel::Nullable<MutableCharSpan> & value, size_t maxLen);

Protocols::InteractionModel::Status InternalReadAttribute(const ConcreteAttributePath & path, MutableByteSpan & value,
                                                          size_t maxLen);

Protocols::InteractionModel::Status InternalReadAttribute(const ConcreteAttributePath & path,
                                                          DataModel::Nullable<MutableByteSpan> & value, size_t maxLen);

template <typename X>
Protocols::InteractionModel::Status InternalWriteAttribute(const ConcreteAttributePath & path, const X & x,
                                                           std::optional<bool> markDirty = std::nullopt)
{
    WriteOperation request(path);
    AttributeValueDecoder decoder = request.DecoderFor(x, sizeof(X));
    return request.WriteEncodedAttribute(decoder, markDirty);
}

Protocols::InteractionModel::Status InternalWriteAttribute(const ConcreteAttributePath & path, const CharSpan & x, size_t maxLen,
                                                           std::optional<bool> markDirty = std::nullopt);

Protocols::InteractionModel::Status InternalWriteAttribute(const ConcreteAttributePath & path, const ByteSpan & x, size_t maxLen,
                                                           std::optional<bool> markDirty = std::nullopt);

Protocols::InteractionModel::Status InternalWriteAttributeNull(const ConcreteAttributePath & path,
                                                               std::optional<bool> markDirty = std::nullopt);

} // namespace app
} // namespace chip
