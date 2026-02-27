// @author Thomas Cuvillier
// @organization Telos Foundation
// @contract bridge
// @version v1.0

// EOSIO
#include <eosio/eosio.hpp>
#include <eosio/singleton.hpp>
#include <eosio/crypto.hpp>
#include <eosio/transaction.hpp>
#include <eosio/asset.hpp>
#include <eosio/system.hpp>

// EXTERNAL
#include <intx/base.hpp>
#include <rlp/rlp.hpp>
#include <ecc/uECC.c>
#include <keccak256/k.c>
#include <boost/multiprecision/cpp_int.hpp>

// TELOS EVM
#include <constants.hpp>
#include <evm_util.hpp>
#include <datastream.hpp>
#include <evm_tables.hpp>
#include <tables.hpp>

using namespace std;
using namespace eosio;
using namespace evm_bridge;
using namespace intx;

namespace evm_bridge
{
    class [[eosio::contract(CONTRACT_NAME)]] tokenbridge : public contract {
        public:
            using contract::contract;
            tokenbridge(name self, name code, datastream<const char*> ds) : contract(self, code, ds), config_bridge(self, self.value), config(EVM_SYSTEM_CONTRACT, EVM_SYSTEM_CONTRACT.value) { };
            ~tokenbridge() {};

            //======================== Helper functions =====================
            uint64_t calc_fee(const eosio::name& account, const uint256_t& quantity);

            //======================== Admin actions ========================
            // intialize the contract
            [[eosio::action]] void init(eosio::checksum160 bridge_address, eosio::checksum160 register_address, std::string version, eosio::name admin);

            [[eosio::action]] void modifyconfig(
                std::optional<eosio::checksum160> bridge_address,
                std::optional<eosio::checksum160> register_address,
                std::optional<eosio::name> admin,
                std::optional<std::string> version,
                std::optional<uint64_t> max_fee_percentage,
                std::optional<uint64_t> min_transfer_qty,
                std::optional<uint64_t> min_fee_qty,
                std::optional<eosio::name> blockbastards_reserve_account,
                std::optional<std::vector<eosio::name>> fee_whitelist
            );

            [[eosio::action]] void clearerrorlog(std::optional<std::vector<uint64_t>> ids);

            //======================== Token bridge actions ========================

            // Notifies Antelope of a refund in EVM
            [[eosio::action]] void refundnotify();

            // Notifies Antelope of a bridge request in EVM
            [[eosio::action]] void reqnotify();
            
            // Bridge to EVM
            [[eosio::on_notify("*::transfer")]] void bridge(eosio::name from, eosio::name to, eosio::asset quantity, std::string memo);

            config_singleton_bridge config_bridge;
            config_singleton_evm config;

            #if (TESTING == true)
                [[eosio::action]] void clear()
                {
                    require_auth(get_self());
                    requests_table requests(get_self(), get_self().value);
                    auto itr = requests.end();
                    while (requests.begin() != itr)
                    {
                      itr = requests.erase(--itr);
                    }
                    refunds_table refunds(get_self(), get_self().value);
                    auto itr_refunds = refunds.end();
                    while (refunds.begin() != itr_refunds)
                    {
                      itr_refunds = refunds.erase(--itr_refunds);
                    }
                }

                [[eosio::action]] void clearconfig()
                {
                    require_auth(get_self());
                    config_singleton_bridge bridgeconfig(get_self(), get_self().value);
                    bridgeconfig.remove();
                }
            #endif
    };

    asset get_stake(name account);
    asset get_locked_stake(name account);
    asset get_balance(name account);
}
