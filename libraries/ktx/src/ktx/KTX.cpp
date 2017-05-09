//
//  KTX.cpp
//  ktx/src/ktx
//
//  Created by Zach Pomerantz on 2/08/2017.
//  Copyright 2017 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "KTX.h"

#include <algorithm> //min max and more
#include <QDebug>

using namespace ktx;

uint32_t Header::evalPadding(size_t byteSize) {
    //auto padding = byteSize % PACKING_SIZE;
 //   return (uint32_t) (padding ? PACKING_SIZE - padding : 0);
    return (uint32_t) (3 - (byteSize + 3) % PACKING_SIZE);// padding ? PACKING_SIZE - padding : 0);
}


const Header::Identifier ktx::Header::IDENTIFIER {{
    0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A
}};

Header::Header() {
    memcpy(identifier, IDENTIFIER.data(), IDENTIFIER_LENGTH);
}

uint32_t Header::evalMaxDimension() const {
    return std::max(getPixelWidth(), std::max(getPixelHeight(), getPixelDepth()));
}

uint32_t Header::evalPixelOrBlockWidth(uint32_t level) const {
    auto pixelWidth = std::max(getPixelWidth() >> level, 1U);
    if (getGLType() == GLType::COMPRESSED_TYPE) {
        return (pixelWidth + 3) / 4;
    } else {
        return pixelWidth;
    }
}
uint32_t Header::evalPixelOrBlockHeight(uint32_t level) const {
    auto pixelWidth = std::max(getPixelHeight() >> level, 1U);
    if (getGLType() == GLType::COMPRESSED_TYPE) {
        auto format = getGLInternaFormat_Compressed();
        switch (format) {
            case GLInternalFormat_Compressed::COMPRESSED_SRGB_S3TC_DXT1_EXT: // BC1
            case GLInternalFormat_Compressed::COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT: // BC1A
            case GLInternalFormat_Compressed::COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT: // BC3
            case GLInternalFormat_Compressed::COMPRESSED_RED_RGTC1: // BC4
            case GLInternalFormat_Compressed::COMPRESSED_RG_RGTC2: // BC5
                return (pixelWidth + 3) / 4;
            default:
                throw std::runtime_error("Unknown format");
        }
    } else {
        return pixelWidth;
    }
}
uint32_t Header::evalPixelOrBlockDepth(uint32_t level) const {
    return std::max(getPixelDepth() >> level, 1U);
}

size_t Header::evalPixelOrBlockSize() const {
    if (getGLType() == GLType::COMPRESSED_TYPE) {
        auto format = getGLInternaFormat_Compressed();
        if (format == GLInternalFormat_Compressed::COMPRESSED_SRGB_S3TC_DXT1_EXT) {
            return 8;
        } else if (format == GLInternalFormat_Compressed::COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT) {
            return 8;
        } else if (format == GLInternalFormat_Compressed::COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT) {
            return 16;
        } else if (format == GLInternalFormat_Compressed::COMPRESSED_RED_RGTC1) {
            return 8;
        } else if (format == GLInternalFormat_Compressed::COMPRESSED_RG_RGTC2) {
            return 16;
        }
    } else {
        auto baseFormat = getGLBaseInternalFormat();
        if (baseFormat == GLBaseInternalFormat::RED) {
            return 1;
        } else if (baseFormat == GLBaseInternalFormat::RG) {
            return 2;
        } else if (baseFormat == GLBaseInternalFormat::RGB) {
            return 3;
        } else if (baseFormat == GLBaseInternalFormat::RGBA) {
            return 4;
        }
    }

    qWarning() << "Unknown ktx format: " << glFormat << " " << glBaseInternalFormat << " " << glInternalFormat;
    return 0;
}

size_t Header::evalRowSize(uint32_t level) const {
    auto pixWidth = evalPixelOrBlockWidth(level);
    auto pixSize = evalPixelOrBlockSize();
    if (pixSize == 0) {
        return 0;
    }
    auto netSize = pixWidth * pixSize;
    auto padding = evalPadding(netSize);
    return netSize + padding;
}
size_t Header::evalFaceSize(uint32_t level) const {
    auto pixHeight = evalPixelOrBlockHeight(level);
    auto pixDepth = evalPixelOrBlockDepth(level);
    auto rowSize = evalRowSize(level);
    return pixDepth * pixHeight * rowSize;
}
size_t Header::evalImageSize(uint32_t level) const {
    auto faceSize = evalFaceSize(level);
    if (numberOfFaces == NUM_CUBEMAPFACES && numberOfArrayElements == 0) {
        return faceSize;
    } else {
        return (getNumberOfSlices() * numberOfFaces * faceSize);
    }
}


size_t KTXDescriptor::getValueOffsetForKey(const std::string& key) const {
    size_t offset { 0 };
    for (auto& kv : keyValues) {
        if (kv._key == key) {
            return offset + ktx::KV_SIZE_WIDTH + kv._key.size() + 1;
        }
        offset += kv.serializedByteSize();
    }
    return 0;
}

ImageDescriptors Header::generateImageDescriptors() const {
    ImageDescriptors descriptors;

    size_t imageOffset = 0;
    for (uint32_t level = 0; level < numberOfMipmapLevels; ++level) {
        auto imageSize = static_cast<uint32_t>(evalImageSize(level));
        if (imageSize == 0) {
            return ImageDescriptors();
        }
        ImageHeader header {
            numberOfFaces == NUM_CUBEMAPFACES,
            imageOffset,
            imageSize,
            0
        };

        imageOffset += (imageSize * numberOfFaces) + ktx::IMAGE_SIZE_WIDTH;

        ImageHeader::FaceOffsets offsets;
        // TODO Add correct face offsets
        for (uint32_t i = 0; i < numberOfFaces; ++i) {
            offsets.push_back(0);
        }
        descriptors.push_back(ImageDescriptor(header, offsets));
    }

    return descriptors;
}


KeyValue::KeyValue(const std::string& key, uint32_t valueByteSize, const Byte* value) :
    _byteSize((uint32_t) key.size() + 1 + valueByteSize), // keyString size + '\0' ending char + the value size
    _key(key),
    _value(valueByteSize)
{
    if (_value.size() && value) {
        memcpy(_value.data(), value, valueByteSize);
    }
}

KeyValue::KeyValue(const std::string& key, const std::string& value) :
    KeyValue(key, (uint32_t) value.size(), (const Byte*) value.data())
{

}

uint32_t KeyValue::serializedByteSize() const {
    return (uint32_t) (sizeof(uint32_t) + _byteSize + Header::evalPadding(_byteSize));
}

uint32_t KeyValue::serializedKeyValuesByteSize(const KeyValues& keyValues) {
    uint32_t keyValuesSize = 0;
    for (auto& keyval : keyValues) {
        keyValuesSize += keyval.serializedByteSize();
    }
    return (keyValuesSize + Header::evalPadding(keyValuesSize));
}


KTX::KTX() {
}

KTX::~KTX() {
}

void KTX::resetStorage(const StoragePointer& storage) {
    _storage = storage;
    if (_storage->size() >= sizeof(Header)) {
        memcpy(&_header, _storage->data(), sizeof(Header));
    }
}

const Header& KTX::getHeader() const {
    return _header;
}


size_t KTX::getKeyValueDataSize() const {
    return _header.bytesOfKeyValueData;
}

size_t KTX::getTexelsDataSize() const {
    if (!_storage) {
        return 0;
    }
    return  (_storage->data() + _storage->size()) - getTexelsData();
}

const Byte* KTX::getKeyValueData() const {
    if (!_storage) {
        return nullptr;
    }
    return (_storage->data() + sizeof(Header));
}

const Byte* KTX::getTexelsData() const {
    if (!_storage) {
        return nullptr;
    }
    return (_storage->data() + sizeof(Header) + getKeyValueDataSize());
}

storage::StoragePointer KTX::getMipFaceTexelsData(uint16_t mip, uint8_t face) const {
    storage::StoragePointer result;
    if (mip < _images.size()) {
        const auto& faces = _images[mip];
        if (face < faces._numFaces) {
            auto faceOffset = faces._faceBytes[face] - _storage->data();
            auto faceSize = faces._faceSize;
            result = _storage->createView(faceSize, faceOffset);
        }
    }
    return result;
}

size_t KTXDescriptor::getMipFaceTexelsSize(uint16_t mip, uint8_t face) const {
    size_t result { 0 };
    if (mip < images.size()) {
        const auto& faces = images[mip];
        if (face < faces._numFaces) {
            result = faces._faceSize;
        }
    }
    return result;
}

size_t KTXDescriptor::getMipFaceTexelsOffset(uint16_t mip, uint8_t face) const {
    size_t result { 0 };
    if (mip < images.size()) {
        const auto& faces = images[mip];
        if (face < faces._numFaces) {
            result = faces._faceOffsets[face];
        }
    }
    return result;
}

ImageDescriptor Image::toImageDescriptor(const Byte* baseAddress) const {
    FaceOffsets offsets;
    offsets.resize(_faceBytes.size());
    for (size_t face = 0; face < _numFaces; ++face) {
        offsets[face] = _faceBytes[face] - baseAddress;
    }
    // Note, implicit cast of *this to const ImageHeader&
    return ImageDescriptor(*this, offsets);
}

Image ImageDescriptor::toImage(const ktx::StoragePointer& storage) const {
    FaceBytes faces;
    faces.resize(_faceOffsets.size());
    for (size_t face = 0; face < _numFaces; ++face) {
        faces[face] = storage->data() + _faceOffsets[face];
    }
    // Note, implicit cast of *this to const ImageHeader&
    return Image(*this, faces);
}

KTXDescriptor KTX::toDescriptor() const {
    ImageDescriptors newDescriptors;
    auto storageStart = _storage ? _storage->data() : nullptr;
    for (size_t i = 0; i < _images.size(); ++i) {
        newDescriptors.emplace_back(_images[i].toImageDescriptor(storageStart));
    }
    return { this->_header, this->_keyValues, newDescriptors };
}

KTX::KTX(const StoragePointer& storage, const Header& header, const KeyValues& keyValues, const Images& images)
    : _header(header), _storage(storage), _keyValues(keyValues), _images(images) {
}
