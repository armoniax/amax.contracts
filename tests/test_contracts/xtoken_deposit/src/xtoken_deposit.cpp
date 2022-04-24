#include <amax.xtoken/amax.xtoken.hpp>

namespace amax_xtoken {

#ifndef ASSERT
    #define ASSERT(exp) eosio::CHECK(exp, #exp)
#endif

    #define CHECK(exp, msg) { if (!(exp)) eosio::CHECK(false, msg); }

    string to_string( const symbol& s ) {
        return std::to_string(s.precision()) + "," + s.code().to_string();
    }

    void deposit::ontransfer(const name &from,
                          const name &to,
                          const asset &quantity,
                          const string &memo)
    {
        if (from == get_self() || to != get_self()) return;

        CHECK(from != to, "cannot transfer to self");
        const auto &bank = get_first_receiver();
        if (bank == SYS_BANK && quantity.symbol.code() == DEPOSIT_SYMBOL.code()) {
            require_auth(bank);
            CHECK(is_account(from), "from account does not exist");
            CHECK(quantity.symbol == DEPOSIT_SYMBOL, "deposit symbol precision mismatch");
            CHECK(quantity.amount > 0, "must transfer positive quantity");
            add_balance(from, quantity, get_self()); // pay ram by contract self
        }


    }


    void deposit::withdraw(const name& owner, const name& to, const asset &quantity, const string &memo)
    {
        require_auth(owner);
        CHECK(is_account(to), "to account does not exist");
        CHECK(DEPOSIT_SYMBOL == quantity.symbol, "unsupported deposit symbol:" + to_string(quantity.symbol))
        CHECK(quantity.is_valid(), "invalid quantity");
        CHECK(quantity.amount > 0, "must withdraw positive quantity");
        CHECK(memo.size() <= 256, "memo has more than 256 bytes");
        auto payer = has_auth(to) ? to : from;
        sub_balance(from, quantity, payer);
    }

    void deposit::onpayfee(const name &from, const name &to, const name& fee_receiver, const asset &fee, const string &memo) {

        if (from == get_self() || to != get_self()) return;

        CHECK(from != to, "cannot transfer to self");
        if (bank == SYS_BANK && quantity.symbol.code() == DEPOSIT_SYMBOL.code()) {
            require_auth(bank);
            CHECK(quantity.symbol == DEPOSIT_SYMBOL, "deposit symbol precision mismatch");
            if (fee.amount != 0) {
                sub_balance(from, fee, same_payer);
            }
        }
    }

    void deposit::sub_balance(const name &owner, const asset &value, const name &ram_payer)
    {
        accounts from_accts(get_self(), owner.value);
        const auto &from = from_accts.get(value.symbol.code().raw(), "no balance object found");
        CHECK(from.balance.amount >= value.amount, "overdrawn balance");

        from_accts.modify(from, ram_payer, [&](auto &a) {
            a.balance -= value;
        });

    }

    void deposit::add_balance(const name &owner, const asset &value, const name &ram_payer)
    {
        accounts to_accts(get_self(), owner.value);
        auto to = to_accts.find(value.symbol.code().raw());
        if (to == to_accts.end()) {
            to_accts.emplace(ram_payer, [&](auto &a) {
                a.balance = value;
            });
        }
        else
        {
            to_accts.modify(to, same_payer, [&](auto &a) {
                a.balance += value;
            });
        }
    }

    void deposit::open(const name &owner, const symbol &symbol, const name &ram_payer)
    {
        require_auth(ram_payer);
        CHECK(is_account(owner), "owner account does not exist");
        CHECK(DEPOSIT_SYMBOL == fee.symbol, "unsupported pay fee symbol:" + to_string(quantity.symbol))
        open_account(owner, symbol, ram_payer);
    }

    bool deposit::open_account(const name &owner, const symbol &symbol, const name &ram_payer) {
        accounts accts(get_self(), owner.value);
        auto it = accts.find(symbol.code().raw());
        if (it == accts.end())
        {
            accts.emplace(ram_payer, [&](auto &a)
                          { a.balance = asset{0, symbol}; });
            return true;
        }
        return false;
    }

    void deposit::close(const name &owner, const symbol &symbol)
    {
        require_auth(owner);

        auto sym_code_raw = symbol.code().raw();
        CHECK(DEPOSIT_SYMBOL == fee.symbol, "unsupported pay fee symbol:" + to_string(quantity.symbol))
        accounts accts(get_self(), owner.value);
        auto it = accts.find(sym_code_raw);
        CHECK(it != accts.end(), "Balance row already deleted or never existed. Action won't have any effect.");
        CHECK(!is_account_frozen(st, owner, *it), "account is frozen");
        CHECK(it->balance.amount == 0, "Cannot close because the balance is not zero.");
        accts.erase(it);
    }


} /// namespace amax_xtoken
