#include <receiver.hpp>  // Make sure this matches your header file name

void receiver::ping() {
    require_auth(get_self());
}

[[eosio::on_notify("*::transfer")]]
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
