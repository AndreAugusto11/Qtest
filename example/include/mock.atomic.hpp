#pragma once
#include <eosio/eosio.hpp>

using namespace eosio;
using namespace std;

class [[eosio::contract("mock.atomic")]] mockatomic : public contract {
  public:
    using contract::contract;

    struct attribute {
        string key;
        string value;
    };

    struct [[eosio::table]] nft_asset {
        uint64_t asset_id;
        name owner;
        name collection;
        name schema;
        vector<attribute> immutable_data;
        vector<attribute> mutable_data;
        
        uint64_t primary_key() const { return asset_id; }
    };

    using assets_table = multi_index<"assets"_n, nft_asset>;

    [[eosio::action]]
    void mint(name to, uint64_t asset_id, name collection, name schema, 
              vector<attribute> immutable_data, vector<attribute> mutable_data);

    [[eosio::action]]
    void transfer(name from, name to, vector<uint64_t> asset_ids, string memo);
};