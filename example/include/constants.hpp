#pragma once

// Adds superpower testing functions (required for running cpp tests/clearing data in contract)
#define TESTING false

// These are on the MACRO below
//#define TOKENCONTRACT "testtoken222"_n
//#define BLOCKBASTARDSRESERVEACCOUNT "jnaikejnaike"_n

// Defined
#define QUDO "QTST", 4

// Macros
#ifndef TOKENCONTRACT_MACRO
#define TOKENCONTRACT_MACRO "qudotoken111"
#endif
static constexpr eosio::name TOKENCONTRACT = eosio::name(TOKENCONTRACT_MACRO);

#ifndef BLOCKBASTARDSRESERVEACCOUNT_MACRO
#define BLOCKBASTARDSRESERVEACCOUNT_MACRO "qudotoken111"
#endif
static constexpr eosio::name BLOCKBASTARDSRESERVEACCOUNT = eosio::name(BLOCKBASTARDSRESERVEACCOUNT_MACRO);

#ifndef BLOCKBASTARDSPREMINEACCOUNT_MACRO
#define BLOCKBASTARDSPREMINEACCOUNT_MACRO "qudotoken111"
#endif
static constexpr eosio::name BLOCKBASTARDSPREMINEACCOUNT = eosio::name(BLOCKBASTARDSPREMINEACCOUNT_MACRO);

#define CHAIN_ID_MACRO CHAIN_ID


// Variables
#define CODEPERMISSION "code"_n
#define BRIDGEFEEMEMO "Bridge fee"

#define CONTRACT_NAME "bridge"

// Crypto
#define MBEDTLS_ASN1_OCTET_STRING 0x04

namespace evm_bridge
{
  struct ChainIDs
  {
    static constexpr size_t TELOS_MAINNET        = 40;
    static constexpr size_t TELOS_TESTNET        = 41;
  };

  static constexpr auto WORD_SIZE       = 32u;
  static constexpr size_t CURRENT_CHAIN_ID = CHAIN_ID_MACRO; // auto from build script
  static constexpr eosio::name EVM_SYSTEM_CONTRACT = eosio::name("eosio.evm"); // DON'T CHANGE, this is the eosio.evm contract name
  
  // NFT Bridge specific signatures
  static constexpr auto EVM_NFT_BRIDGE_TO_SIGNATURE = "7d056de7"; // bridgeTo(address,address,uint256,string)
  static constexpr auto EVM_REQUEST_SUCCESSFUL_SIGNATURE = "0fbc79cd"; // requestSuccessful(uint256)
  static constexpr auto EVM_REFUND_SUCCESSFUL_SIGNATURE = "dc2fdf9f"; // refundSuccessful(uint256)

  static constexpr uint64_t SIGN_REGISTRATION_GAS = 250000; // Todo: find exact needed gas
  static constexpr uint64_t REFUND_CB_GAS = 250000; // Todo: find exact needed gas
  static constexpr uint64_t SUCCESS_CB_GAS = 250000; // Todo: find exact needed gas
  static constexpr uint64_t BRIDGE_GAS = 250000; // Todo: find exact needed gas
  

  // Storage slot constants (must match Solidity contract storage layout)
  // If changed, change the tests as well since they directly set these slots to simulate EVM state
  static constexpr uint8_t STORAGE_BRIDGE_REQUEST_INDEX = 5;
  static constexpr uint8_t STORAGE_BRIDGE_REFUND_INDEX = 6;
  static constexpr uint8_t STORAGE_REGISTER_REQUEST_INDEX = 5;
  static constexpr uint8_t STORAGE_REGISTER_PAIR_INDEX = 4;
}