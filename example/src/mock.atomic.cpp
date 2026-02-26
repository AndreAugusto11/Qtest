#include <mock.atomic.hpp>

[[eosio::action]]
void mockatomic::mint(name to, uint64_t asset_id, name collection, name schema,
                      vector<mockatomic::attribute> immutable_data, 
                      vector<mockatomic::attribute> mutable_data) {
    require_auth(get_self());
    
    assets_table assets(get_self(), get_self().value);
    assets.emplace(get_self(), [&](auto& row) {
        row.asset_id = asset_id;
        row.owner = to;
        row.collection = collection;
        row.schema = schema;
        row.immutable_data = immutable_data;
        row.mutable_data = mutable_data;
    });
}

[[eosio::action]]
void mockatomic::transfer(name from, name to, vector<uint64_t> asset_ids, string memo) {
    require_auth(from);
    
    assets_table assets(get_self(), get_self().value);
    
    for (auto asset_id : asset_ids) {
        auto itr = assets.find(asset_id);
        check(itr != assets.end(), "Asset not found");
        check(itr->owner == from, "Not asset owner");
        
        assets.modify(itr, get_self(), [&](auto& row) {
            row.owner = to;
        });
    }
    
    // Notify recipient
    require_recipient(from);
    require_recipient(to);
}