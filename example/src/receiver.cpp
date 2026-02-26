#include <receiver.hpp>  // Make sure this matches your header file name

void receiver::ping() {
    require_auth(get_self());
}

[[eosio::on_notify("eosio.token::transfer")]]
void receiver::on_transfer(const name& from, const name& to, 
                          const asset& quantity, const string& memo) {
    // Debug: always print something
    print("on_transfer called! ");
    
    // Check conditions
    print("from=", from, " to=", to, " self=", get_self());
    
    if (from == get_self()) {
        print(" [ignoring: from self]");
        return;
    }
    
    if (to != get_self()) {
        print(" [ignoring: to != self]");
        return;
    }
    
    print("Hello, World! Received ", quantity, " from ", from, " with memo: ", memo);

}

[[eosio::on_notify("mock.atomic::transfer")]]
void receiver::on_nft_transfer(const name& from, const name& to,
                              const vector<uint64_t>& asset_ids, const string& memo) {
    if (from == get_self()) return;
    if (to != get_self()) return;
    
    print("Received NFT transfer! from: ", from);
    for (auto asset_id : asset_ids) {
        print(" Asset ID: ", asset_id);
    }
    print(" Collection: coolcol");  
    print(" Schema: schema1");
    print(" Memo: ", memo);
}
