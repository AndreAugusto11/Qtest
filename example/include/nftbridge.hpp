#pragma once

#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <eosio/crypto.hpp>
#include <eosio/singleton.hpp>
#include <eosio/time.hpp>
#include <optional>
#include <string>
#include <vector>

namespace nft_bridge {

using eosio::checksum160;
using eosio::checksum256;
using eosio::current_time_point;
using eosio::check;
using eosio::name;
using eosio::print;
using eosio::require_auth;
using eosio::singleton;
using eosio::time_point_sec;
using eosio::multi_index;
using std::optional;
using std::string;
using std::vector;

struct [[eosio::table]] account_state {
    uint64_t index;
    checksum256 key;
    uint128_t value_low;
    uint128_t value_high;

    uint64_t primary_key() const { return index; }
    checksum256 by_key() const { return key; }
};

using account_state_table = multi_index<
    "accountstate"_n,
    account_state,
    eosio::indexed_by<"bykey"_n, eosio::const_mem_fun<account_state, checksum256, &account_state::by_key>>
>;

class [[eosio::contract("nftbridge")]] nftbridge : public eosio::contract {
  public:
    using contract::contract;

    nftbridge(name receiver, name code, eosio::datastream<const char*> ds)
        : contract(receiver, code, ds),
          config_bridge(receiver, receiver.value) {}

    [[eosio::action]]
    void init(checksum160 bridge_address, checksum160 register_address, string version, name admin);

    [[eosio::action]]
    void modifyconfig(
        optional<checksum160> bridge_address,
        optional<checksum160> register_address,
        optional<name> admin,
        optional<string> version
    );

    [[eosio::action]]
    void clearerrorlog(optional<vector<uint64_t>> ids);

    [[eosio::on_notify("atomicassets::transfer")]]
    void bridge(name from, name to, vector<uint64_t> asset_ids, string memo);

    [[eosio::action]]
    void reqnotify();

    [[eosio::action]]
    void refundnotify();

  private:
    struct [[eosio::table("config")]] config_row {
        string version;
        name admin;
        checksum160 evm_bridge_address;
        checksum160 evm_register_address;
        uint64_t evm_bridge_scope;
        uint64_t evm_register_scope;
    };

    using config_singleton = singleton<"config"_n, config_row>;
    config_singleton config_bridge;

    struct [[eosio::table]] errorlog {
        uint64_t id;
        string message;
        time_point_sec created_at;

        uint64_t primary_key() const { return id; }
    };

    using errorlogs_table = multi_index<"errorlogs"_n, errorlog>;

    struct [[eosio::table]] locked_nft {
        uint64_t id;
        uint64_t asset_id;
        name collection_name;
        name owner;
        string evm_recipient;
        time_point_sec locked_at;

        uint64_t primary_key() const { return id; }
        uint64_t by_asset() const { return asset_id; }
    };

    using locked_nfts_table = multi_index<
        "lockednfts"_n,
        locked_nft,
        eosio::indexed_by<"byasset"_n, eosio::const_mem_fun<locked_nft, uint64_t, &locked_nft::by_asset>>
    >;

    struct [[eosio::table]] request_row {
        uint64_t id;
        uint64_t call_id;
        string sender;
        uint64_t amount;
        string receiver;
        uint8_t evm_decimals;
        time_point_sec created_at;

        uint64_t primary_key() const { return id; }
        uint64_t by_timestamp() const { return created_at.sec_since_epoch(); }
    };

    using requests_table = multi_index<
        "requests"_n,
        request_row,
        eosio::indexed_by<"timestamp"_n, eosio::const_mem_fun<request_row, uint64_t, &request_row::by_timestamp>>
    >;

    struct [[eosio::table]] refund_row {
        uint64_t id;
        uint64_t refund_id;
        uint64_t asset_id;
        string owner;
        time_point_sec created_at;

        uint64_t primary_key() const { return id; }
        uint64_t by_timestamp() const { return created_at.sec_since_epoch(); }
    };

    using refunds_table = multi_index<
        "refunds"_n,
        refund_row,
        eosio::indexed_by<"timestamp"_n, eosio::const_mem_fun<refund_row, uint64_t, &refund_row::by_timestamp>>
    >;

    string get_nft_metadata(uint64_t asset_id);
    vector<uint8_t> get_collection_evm_address(name collection_name);
};

} // namespace nft_bridge
