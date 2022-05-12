#pragma once

#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>
#include <string>
#include <wasm_db.hpp>
#include "amax.xchain/amax.xchain.db.hpp"

namespace amax {

using std::string;
using namespace eosio;
using namespace wasm::db;

/**
 * The `amax.xchain` is Cross-chain (X -> AMAX -> Y) contract
 * 
 */
class [[eosio::contract("amax.xchain")]] xchain : public contract {
private:
   dbc                 _db;
   global_singleton    _global;
   global_t            _gstate;

public:
   using contract::contract;

   xchain(eosio::name receiver, eosio::name code, datastream<const char*> ds):
        _db(_self), contract(receiver, code, ds), _global(_self, _self.value) {
        if (_global.exists()) {
            _gstate = _global.get();

        } else { // first init
            _gstate = global_t{};
            _gstate.admin = _self;
        }
    }

    ~xchain() { _global.set( _gstate, get_self() ); }
   
    
    ACTION reqxintoaddr( const name& account, const name& base_chain );

    ACTION setaddress( const name& account, const name& base_chain, const string& xin_to );

    ACTION mkxinorder(  const name& to, const name& chain_name, const name& coin_name, 
                        const string& txid, const string& xin_from, const string& xin_to,
                        const asset& quantity);

    /**
     * checker to confirm xin order
     */
    ACTION chkxinorder( const name& account, const uint64_t& id);

    ACTION cancelorder( const name& account, const uint64_t& id,const string& cancel_reason );

    /**
     * ontransfer, trigger by recipient of transfer()
     * @param quantity - mirrored asset on AMC
     * @param memo - memo format: $addr@$chain@coin_name&order_no
     *               
     */
    [[eosio::on_notify("*::transfer")]] 
    void ontransfer(name from, name to, asset quantity, string memo);

    ACTION onpaying( const name& account, const uint64_t& id, const string& txid, const string& payno, const string& xout_from );

    ACTION onpaysucc( const name& account, const uint64_t& id );

    /**
     * checker to confirm out order
     */
    ACTION chkxoutorder( const name& account, const uint64_t& id );

    ACTION cancelxout( const name& account, const uint64_t& id );

   private:

    void _check_chain_coin(const name& chain, const name& coin);

    uint8_t _check_base_chain(const name& base_chain);
};
} //namespace apollo
