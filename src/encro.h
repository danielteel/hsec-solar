#pragma once
#include <stdint.h>

uint8_t* encrypt(uint32_t handshake, const uint8_t* data, uint32_t dataLength, uint32_t& encryptedLength, const char* keyString);
uint8_t* decrypt(uint32_t& handshake, const uint8_t* data, uint32_t dataLength, uint32_t& decryptedLength, const char* keyString, bool& error);