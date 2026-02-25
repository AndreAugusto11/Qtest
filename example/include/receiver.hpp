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

    [[eosio::on_notify("*::transfer")]]
    void on_transfer(const name& from, const name& to, 
                    const asset& quantity, const string& memo);
};