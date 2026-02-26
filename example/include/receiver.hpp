#pragma once
#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>

using namespace eosio;
using namespace std;

class [[eosio::contract("receiver")]] receiver : public contract {
  public:
    using contract::contract;

    [[eosio::action]]
    void ping();

    [[eosio::on_notify("token::transfer")]]
    void on_transfer(const name& from, const name& to, 
                    const asset& quantity, const string& memo);

    [[eosio::on_notify("nfttoken::transfer")]]
    void on_nft_transfer(const name& from, const name& to,
                        const vector<uint64_t>& asset_ids, const string& memo);
};