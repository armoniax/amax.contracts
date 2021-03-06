#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>
#include <string>

#include "amax.mulsign.hpp"

using namespace amax;

ACTION mulsign::init(const name& fee_collector, const asset& wallet_fee) {
   require_auth( _self );

   CHECKC( wallet_fee.symbol == SYS_SYMBOL, err::SYMBOL_MISMATCH, "requir fee type: AMAX");
   CHECKC( wallet_fee.amount > 0, err::NOT_POSITIVE, "fee must be positive");
   CHECKC( is_account(fee_collector), err::ACCOUNT_INVALID, "invalid fee collector: " + fee_collector.to_string() )
   
   if(_gstate.fee_collector == name()) create_wallet(fee_collector, "amax.daodev");
   
   _gstate.fee_collector = fee_collector;
   _gstate.wallet_fee = wallet_fee;
}

ACTION mulsign::setmulsigner(const name& issuer, const uint64_t& wallet_id, const name& mulsigner, const uint32_t& weight) {
   require_auth( issuer );
   CHECKC( is_account(mulsigner), err::ACCOUNT_INVALID, "invalid mulsigner: " + mulsigner.to_string() )
   CHECKC( weight>0, err::NOT_POSITIVE, "weight must be a positive number")

   auto wallet = wallet_t(wallet_id);
   CHECKC( _db.get(wallet), err::RECORD_NOT_FOUND, "wallet not found: " + to_string(wallet_id) )
   int64_t elapsed =  current_time_point().sec_since_epoch() - wallet.created_at.sec_since_epoch();
   CHECKC((wallet.creator == issuer && elapsed < seconds_per_day) || issuer == get_self(), err::NO_AUTH, "only creator or propose proposal to add cosinger" )
   //notify user when add
   if( wallet.mulsigners.count(mulsigner) == 0 ) require_recipient(mulsigner);
   wallet.mulsigners[mulsigner] = weight;
   uint32_t total_weight = 0;
   for (const auto& item : wallet.mulsigners) {
      total_weight += item.second;
   }
   CHECKC( total_weight >= wallet.mulsign_m, err::OVERSIZED, "total weight " + to_string(wallet.mulsign_n) + "must be grater than  m: " + to_string(wallet.mulsign_m) );
   wallet.mulsign_n = total_weight;
   wallet.updated_at = current_time_point();
   _db.set( wallet, issuer );
}

ACTION mulsign::setmulsignm(const name& issuer, const uint64_t& wallet_id, const uint32_t& mulsignm){
   require_auth( issuer );

   CHECKC( mulsignm > 0, err::PARAM_ERROR, "m must be a positive num");
   auto wallet = wallet_t(wallet_id);
   CHECKC( _db.get(wallet), err::RECORD_NOT_FOUND, "wallet not found: " + to_string(wallet_id) )
   int64_t elapsed =  current_time_point().sec_since_epoch() - wallet.created_at.sec_since_epoch();
   CHECKC( (wallet.creator == issuer && elapsed < seconds_per_day) || issuer == get_self(), err::NO_AUTH, "only creator or proposal allowed to edit m")
   CHECKC( mulsignm <= wallet.mulsign_n, err::OVERSIZED, "total weight " + to_string(wallet.mulsign_n) + "must be grater than  m: " + to_string(mulsignm));
   
   wallet.mulsign_m = mulsignm;
   wallet.updated_at = current_time_point();
   _db.set( wallet, issuer );
}

ACTION mulsign::setproexpiry(const name& issuer, const uint64_t wallet_id, const uint64_t& expiry_sec) {
   require_auth( issuer );

   auto wallet = wallet_t(wallet_id);
   CHECKC( _db.get(wallet), err::RECORD_NOT_FOUND, "wallet not found: " + to_string(wallet_id) )
   int64_t elapsed =  current_time_point().sec_since_epoch() - wallet.created_at.sec_since_epoch();
   CHECKC( (wallet.creator == issuer && elapsed < seconds_per_day) || issuer == get_self(), err::NO_AUTH, "only creator or proposal allowed to set expiry")
   
   wallet.proposal_expiry_sec = expiry_sec;
   _db.set( wallet, issuer );
}

ACTION mulsign::delmulsigner(const name& issuer, const uint64_t& wallet_id, const name& mulsigner) {
   require_auth( issuer );

   auto wallet = wallet_t(wallet_id);
   CHECKC( _db.get( wallet ), err::RECORD_NOT_FOUND, "wallet not found: " + to_string(wallet_id) )
   int64_t elapsed = current_time_point().sec_since_epoch() - wallet.created_at.sec_since_epoch();
   CHECKC( (wallet.creator == issuer && elapsed < seconds_per_day) || issuer == get_self(), err::NO_AUTH, "only creator or proposal allowed to del cosinger")
   CHECKC( wallet.mulsigners.count(mulsigner)==1, err::RECORD_NOT_FOUND, "cannot found mulsigner: "+mulsigner.to_string());

   wallet.mulsigners.erase(mulsigner);
   uint32_t total_weight = 0;
   for (const auto& item : wallet.mulsigners) {
      total_weight += item.second;
   }
   CHECKC( total_weight >= wallet.mulsign_m, err::OVERSIZED, "total weight " + to_string(wallet.mulsign_n) + "must be grater than  m: " + to_string(wallet.mulsign_m) );
   wallet.mulsign_n = total_weight;
   wallet.updated_at = current_time_point();
   _db.set( wallet, issuer );
   require_recipient(mulsigner);
}

void mulsign::ontransfer(const name& from, const name& to, const asset& quantity, const string& memo) {
   if(from == get_self() || to != get_self()) return;
   CHECKC( from != to, err::ACCOUNT_INVALID,"cannot transfer to self" );
   CHECKC( quantity.amount > 0, err::PARAM_ERROR, "non-positive quantity not allowed" )
   CHECKC( memo != "", err::PARAM_ERROR, "empty memo!" )

   auto bank_contract = get_first_receiver();

   vector<string_view> memo_params = split(memo, ":");
   if (memo_params[0] == "create" && memo_params.size() == 2) {
      string title = string(memo_params[1]);
      CHECKC( title.length() < 1024, err::OVERSIZED, "wallet title too long" )
      CHECKC( bank_contract == SYS_BANK && quantity.symbol == SYS_SYMBOL, err::PARAM_ERROR, "non-sys-symbol" )
      CHECKC( quantity >= _gstate.wallet_fee, err::FEE_INSUFFICIENT, "insufficient wallet fee: " + quantity.to_string() )

      COLLECTFEE( from, _gstate.fee_collector, quantity )

      create_wallet(from, title);
      lock_funds(0, bank_contract, quantity);

   } else if (memo_params[0] == "lock" && memo_params.size() == 2) {
      auto wallet_id = (uint64_t) stoi(string(memo_params[1]));
      lock_funds(wallet_id, bank_contract, quantity);

   } else {
      CHECKC(false, err::PARAM_ERROR, "invalid memo" )
   }
}

ACTION mulsign::collectfee(const name& from, const name& to, const asset& quantity) {
   require_auth( _self );
   require_recipient( _gstate.fee_collector );
}
   
ACTION mulsign::propose(const name& issuer, 
                   const uint64_t& wallet_id, 
                   const name& type, 
                   const map<string, string>& params, 
                   const string& excerpt, 
                   const string& description,
                   const uint32_t& duration) {
   require_auth( issuer );

   auto wallet = wallet_t(wallet_id);
   CHECKC( _db.get( wallet ), err::RECORD_NOT_FOUND, "wallet not found: " + to_string(wallet_id) )
   CHECKC( wallet.mulsigners.count(issuer), err::ACCOUNT_INVALID, "only mulsigner can propose actions" )
   CHECKC( wallet.proposal_expiry_sec >= duration && duration >= 0, err::OVERSIZED, "duration shoule less than expiry_sec" )
   const auto expiry = duration ==0? wallet.proposal_expiry_sec: duration;
   check_proposal_params(type, params);
   if(type == proposal_type::transfer){
      asset quantity = asset_from_string(params.at("quantity"));
      name to = name(params.at("to"));
      name bank_contract = name(params.at("contract"));
      CHECKC( to != get_self(), err::ACCOUNT_INVALID, "cannot trans to self")
      CHECKC( is_account(to), err::ACCOUNT_INVALID, "account invalid: " + to.to_string());
      CHECKC( is_account(bank_contract), err::ACCOUNT_INVALID, "contract invalid: " + bank_contract.to_string())
      
      auto ex_asset = extended_asset(quantity, bank_contract);
      const auto& symb = ex_asset.get_extended_symbol();
      CHECKC( wallet.assets.count(symb), err::PARAM_ERROR,
         "symbol not found in wallet: " + to_string(ex_asset) )
      CHECKC( ex_asset.quantity.amount > 0, err::PARAM_ERROR, "withdraw quantity must be positive" )
      auto avail_quant = wallet.assets[ symb ];
      CHECKC( ex_asset.quantity.amount <= avail_quant, err::OVERSIZED, "overdrawn proposal: " + ex_asset.quantity.to_string() + " > " + to_string(avail_quant) )
   }
   else if(type == proposal_type::setmulsignm){
      uint32_t m = to_uint32(params.at("m"), "error type of m");
      CHECKC( m <= wallet.mulsign_n, err::OVERSIZED, "total weight oversize than m: " + to_string(wallet.mulsign_m) )
      CHECKC( m >0, err::OVERSIZED, "m must be a positive number" )
   }
   else if(type == proposal_type::setmulsigner){
      uint32_t weight = to_uint32(params.at("weight"), "error type of weight");
      name mulsigner = name(params.at("mulsigner"));
      CHECKC( is_account(mulsigner), err::ACCOUNT_INVALID, "account invalid: " + mulsigner.to_string());
   }
   else if(type == proposal_type::delmulsigner){
      name mulsigner = name(params.at("mulsigner"));
      CHECKC( is_account(mulsigner), err::ACCOUNT_INVALID, "account invalid: " + mulsigner.to_string());
      CHECKC( wallet.mulsigners.count(mulsigner), err::ACCOUNT_INVALID, "account not in mulsigners: " + mulsigner.to_string());
   }
   else {
      CHECKC( false, err::PARAM_ERROR, "Unsupport proposal type")
   }

   CHECKC( excerpt.length() < 1024, err::OVERSIZED, "excerpt length >= 1024" )
   CHECKC( description.length() < 2048, err::OVERSIZED, "description length >= 2048" )
   
   auto proposals = proposal_t::idx_t(_self, _self.value);
   auto pid = proposals.available_primary_key();
   auto proposal = proposal_t(pid);
   proposal.wallet_id = wallet_id;
   proposal.type = type;
   proposal.params = params;
   proposal.proposer = issuer;
   proposal.excerpt = excerpt;
   proposal.description = description;
   proposal.status = proposal_status::PROPOSED;
   proposal.created_at = current_time_point();
   proposal.expired_at = proposal.created_at + expiry;

   _db.set(proposal, issuer);
}

ACTION mulsign::cancel(const name& issuer, const uint64_t& proposal_id) {
   require_auth( issuer );

   const auto& now = current_time_point();
   auto proposal = proposal_t(proposal_id);
   CHECKC( _db.get( proposal ), err::RECORD_NOT_FOUND, "proposal not found: " + to_string(proposal_id) )
   CHECKC( proposal.proposer == issuer, err::NO_AUTH, "issuer is not proposer" )
   CHECKC( proposal.approvers.size() == 0, err::NO_AUTH, "proposal already appoved" )
   CHECKC( proposal.expired_at > now, err::NO_AUTH, "proposal already expired" )

   proposal.updated_at = now;
   proposal.status = proposal_status::CANCELED;
   _db.set( proposal );
}

/**
 * @brief only mulsigner can submit the proposal: the m-th of n mulsigner will trigger its execution
 * @param issuer
 * @param
 */
ACTION mulsign::respond(const name& issuer, const uint64_t& proposal_id, uint8_t vote) {
   require_auth( issuer );

   const auto& now = current_time_point();
   auto proposal = proposal_t(proposal_id);
   CHECKC( _db.get( proposal ), err::RECORD_NOT_FOUND, "proposal not found: " + to_string(proposal_id) )
   CHECKC( proposal.status == proposal_status::PROPOSED || proposal.status == proposal_status::APPROVED, 
      err::STATUS_ERROR, "proposal can not be approved at status: " + proposal.status.to_string() )
   CHECKC( proposal.expired_at >= now, err::TIME_EXPIRED, "the proposal already expired" )
   CHECKC( !proposal.approvers.count(issuer), err::ACTION_REDUNDANT, "issuer (" + issuer.to_string() +") already approved" )

   auto wallet = wallet_t(proposal.wallet_id);
   CHECKC( _db.get( wallet ), err::RECORD_NOT_FOUND, "wallet not found: " + to_string(proposal.wallet_id) )
   CHECKC( wallet.mulsigners.count(issuer), err::NO_AUTH, "issuer (" + issuer.to_string() +") not allowed to approve" )
   CHECKC( vote == proposal_vote::PROPOSAL_AGAINST || vote == proposal_vote::PROPOSAL_FOR, err::PARAM_ERROR, "unsupport result" )

   proposal.approvers.insert(map<name,uint32_t>::value_type(issuer, vote?wallet.mulsigners[issuer]:0));
   if(vote == proposal_vote::PROPOSAL_FOR) proposal.recv_votes += wallet.mulsigners[issuer];
   proposal.updated_at = now;
   proposal.status = proposal_status::APPROVED;
   _db.set(proposal, issuer);
}



ACTION mulsign::execute(const name& issuer, const uint64_t& proposal_id) {
   require_auth( issuer );
   const auto& now = current_time_point();
   auto proposal = proposal_t(proposal_id);
   CHECKC( _db.get( proposal ), err::RECORD_NOT_FOUND, "proposal not found: " + to_string(proposal_id) )
   CHECKC( proposal.status == proposal_status::APPROVED, err::STATUS_ERROR,
           "proposal can not be executed at status: " + proposal.status.to_string() )

   auto wallet = wallet_t(proposal.wallet_id);
   CHECKC( _db.get( wallet ), err::RECORD_NOT_FOUND, "wallet not found: " + to_string(proposal.wallet_id) )
   CHECKC( proposal.recv_votes >= wallet.mulsign_m, err::NO_AUTH, "insufficient votes" )

   execute_proposal(wallet, proposal);
   proposal.updated_at = now;
   proposal.status = proposal_status::EXECUTED;
   _db.set(proposal);
}

void mulsign::create_wallet(const name& creator, const string& title) {
   auto mwallets = wallet_t::idx_t(_self, _self.value);
   auto wallet_id = mwallets.available_primary_key();
   if (wallet_id == 0) {
      CHECKC(creator == _gstate.fee_collector, err::FIRST_CREATOR, "the first creator must be fee_collector: " + _gstate.fee_collector.to_string());
   }

   auto wallet = wallet_t(wallet_id);
   wallet.title = title;
   wallet.mulsign_m = 1;
   wallet.mulsign_n = 1;
   wallet.mulsigners[creator] = 1;
   wallet.creator = creator;
   wallet.created_at = current_time_point();

   _db.set( wallet, _self );
}

void mulsign::lock_funds(const uint64_t& wallet_id, const name& bank_contract, const asset& quantity) {
   auto wallet = wallet_t(wallet_id);
   CHECKC( _db.get( wallet ), err::RECORD_NOT_FOUND, "wallet not found: " + to_string(wallet_id) )

   const auto& symb = extended_symbol(quantity.symbol, bank_contract);
   wallet.assets[ symb ] += quantity.amount;
   _db.set( wallet, _self );
}

void mulsign::check_proposal_params(const name& type, const map<string,string>& params){
   if(type == proposal_type::transfer){
      CHECKC( params.count("contract"), err::PARAM_ERROR, "transfer must contain contract")
      CHECKC( params.count("quantity"), err::PARAM_ERROR, "transfer must contain quantity")
      CHECKC( params.count("to"), err::PARAM_ERROR, "transfer must contain to account")
      if( params.count("memo")) CHECKC( params.at("memo").length() < 128, err::OVERSIZED, "memo length >= 1024" )
   }
   else if(type == proposal_type::setmulsignm){
      CHECKC( params.count("m"), err::PARAM_ERROR, "transfer must contain mulsigner's account")
   }
   else if(type == proposal_type::setmulsigner){
      CHECKC( params.count("mulsigner"), err::PARAM_ERROR, "transfer must contain mulsigner's account")
      CHECKC( params.count("weight"), err::PARAM_ERROR, "transfer must contain mulsigner's weight")
   }
   else if(type ==proposal_type::delmulsigner){
      CHECKC( params.count("mulsigner"), err::PARAM_ERROR, "transfer must contain mulsigner's account")
   }
   else {
      CHECKC( false, err::PARAM_ERROR, "Unsupport proposal type")
   }
}

void mulsign::execute_proposal(wallet_t& wallet, proposal_t &proposal) {
   check_proposal_params(proposal.type, proposal.params);
   if(proposal.type == proposal_type::transfer){
      asset quantity = asset_from_string(proposal.params.at("quantity"));
      string memo = proposal.params.count("memo")? proposal.params.at("memo"):"multisign wallet transfer";
      name to = name(proposal.params.at("to"));
      name bank_contract = name(proposal.params.at("contract"));
      CHECKC( is_account(to), err::ACCOUNT_INVALID, "account invalid: " + to.to_string());
      CHECKC( is_account(bank_contract), err::ACCOUNT_INVALID, "contract invalid: " + bank_contract.to_string());

      const auto& symb = extended_symbol( quantity.symbol, bank_contract);
      auto avail_quant = wallet.assets[ symb ];
      CHECKC( quantity.amount <= avail_quant, err::OVERSIZED, "Overdrawn not allowed: " + quantity.to_string() + " > " + to_string(avail_quant) );

      if (quantity.amount == avail_quant) {
         wallet.assets.erase(symb);
      } else {
         wallet.assets[ symb ] -= quantity.amount;
      }

      _db.set(wallet);
      TRANSFER( bank_contract, to, quantity, memo);

   }
   else if(proposal.type == proposal_type::setmulsignm){
      uint32_t m = to_uint32(proposal.params.at("m"), "error type of m");
      SETMULSIGNM(wallet.id, m);
   }
   else if(proposal.type == proposal_type::setmulsigner){
      uint32_t weight = to_uint32(proposal.params.at("weight"), "error type of weight");
      name mulsigner = name(proposal.params.at("mulsigner"));
      SETMULSIGNER(wallet.id, mulsigner, weight);
   }
   else if(proposal.type == proposal_type::delmulsigner){
      name mulsigner = name(proposal.params.at("mulsigner"));
      DELMULSIGNER(wallet.id, mulsigner);
   }
}
