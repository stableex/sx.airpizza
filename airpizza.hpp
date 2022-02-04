#pragma once

#include <eosio/asset.hpp>
#include <sx.pizzalend/pizzalend.hpp>

namespace airpizza {

    using namespace eosio;

    const name id = "airpizza"_n;
    const name code = "air.pizza"_n;
    const std::string description = "Air.Pizza Converter";

    const symbol FEE_SYM = symbol{"F",8};

    struct market_config {
        uint32_t    leverage;
        asset       fee_rate;
    };

    struct [[eosio::table]] market_row {
        symbol                  lptoken;
        vector<extended_symbol> syms;
        vector<asset>           reserves;
        vector<double_t>        prices;
        vector<uint8_t>         lendables;
        uint64_t                lpamount;
        market_config           config;

        uint64_t primary_key() const { return lptoken.code().raw(); }
    };
    typedef eosio::multi_index< "market"_n, market_row > market;

    struct [[eosio::table]] mleverage_row {
        symbol          lptoken;
        uint32_t        leverage;
        uint32_t        begined_at;
        uint32_t        effective_secs;

        uint64_t primary_key() const { return lptoken.code().raw(); }
    };
    typedef eosio::multi_index< "mleverage"_n, mleverage_row > mleverage;

    static int64_t normalize( const asset in, const uint8_t precision)
    {
        return in.amount * static_cast<int64_t>( pow(10, precision - in.symbol.precision() ));
    }

    static asset denormalize( const int64_t amount, const uint8_t precision, const symbol sym)
    {
        return asset{ amount / static_cast<int64_t>(pow( 10, precision - sym.precision() )), sym };
    }

    static uint32_t get_amplifier(uint32_t A0, const symbol_code lptoken) {

        mleverage _mleverage(code, code.value);
        auto it = _mleverage.find(lptoken.raw());
        if(it == _mleverage.end()) return A0;

        const uint64_t now = current_time_point().sec_since_epoch();
        uint64_t A1 = it->leverage;
        uint64_t t0 = it->begined_at;
        uint64_t t1 = it->begined_at + it->effective_secs;
        if(now >= t1) return A1;

        const auto res = ( A1 > A0 )
            ? A0 + (A1 - A0) * (now - t0) / (t1 - t0)
            : A0 - (A0 - A1) * (now - t0) / (t1 - t0);

        return static_cast<uint32_t> (res);
    }

    /**
     * ## STATIC `get_amount_out`
     *
     * Given an input amount of an asset and pair id, returns the calculated return
     *
     * ### params
     *
     * - `{asset} in` - input amount
     * - `{symbol} out_sym` - out symbol
     * - `{symbol} lptoken` - pair id (liquidity token for that market)
     *
     * ### example
     *
     * ```c++
     * // Inputs
     * const asset in = asset { 10000, "USDT" };
     * const symbol out_sym = symbol { "USDE,4" };
     * const symbol_code lptoken = symbol_code { "USDII" }
     *
     * // Calculation
     * const asset out = airpizza::get_amount_out( in, out_sym, lptoken );
     * // => 0.999612
     * ```
     */
    static asset get_amount_out( const asset quantity, const symbol out_sym, const symbol_code lptoken )
    {
        check(quantity.amount > 0, "airpizza: INSUFFICIENT_INPUT_AMOUNT");
        check(lptoken.is_valid(), "airpizza: Invalid liquidity token");

        market _market(code, code.value);
        const auto pool = _market.get(lptoken.raw(), "airpizza: Can't find market");
        check(pool.reserves.size() == 2, "airpizza: Only 2-reserve pools supported");
        check(pool.config.fee_rate.symbol == FEE_SYM, "airpizza: Wrong fee symbol");

        int128_t A = get_amplifier(pool.config.leverage, lptoken);  //get moving amplifier if applicable
        const auto fee = pool.config.fee_rate.amount / 10000;       //"pizza" way to hold a fee
        auto res_in = pool.lendables[0] ? pizzalend::unwrap(pool.reserves[0], true).quantity : pool.reserves[0];
        auto res_out = pool.lendables[1] ? pizzalend::unwrap(pool.reserves[1], true).quantity : pool.reserves[1];
        if(res_in.symbol != quantity.symbol) std::swap(res_in, res_out);

        check(res_in.symbol == quantity.symbol && res_out.symbol == out_sym, "airpizza: wrong pool");
        if(res_in.amount == 0 || res_out.amount == 0) return asset { 0, out_sym };
        uint8_t precision = max(res_in.symbol.precision(), res_out.symbol.precision());

        //normalize reserves and in amount to max precision
        const auto reserve_in = normalize(res_in, precision);
        const auto reserve_out = normalize(res_out, precision);
        const auto amount_in = normalize(quantity, precision);

        //find D based on existing reserves by solving StableSwap invariant equation iteratively
        const uint64_t sum = reserve_in + reserve_out;
        uint128_t D = sum, D_prev = 0;
        int i = 10;
        while ( D != D_prev && i--) {
            uint128_t prod1 = D * D / (reserve_in * 2) * D / (reserve_out * 2);
            D_prev = D;
            D = 2 * D * (2 * A * sum / 10000 + prod1) / (4 * A * D  / 10000 - D + 3 * prod1);
        }

        //find x (reserve_out) based on new reserve_in and D by solving invariant equation iteratively
        const int128_t b = (int128_t) ((reserve_in + amount_in) + (10000 * D / (A * 4))) - (int128_t) D;
        const uint128_t c = D * D / ((reserve_in + amount_in) * 2) * 10000 * D / (A * 8);
        uint128_t x = D, x_prev = 0;
        i = 10;
        while ( x != x_prev && i--) {
            x_prev = x;
            x = (x * x + c) / (2 * x + b);
        }
        uint64_t amount_out = reserve_out - (int64_t)x;

        amount_out -= amount_out * fee / 10000;
        check(amount_out > 0, "airpizza: non-positive OUT");

        const auto out = denormalize( amount_out, precision, out_sym );
        if(pool.lendables[0] || pool.lendables[1]){
            const auto redeemable = pizzalend::get_available_deposit(out_sym);
            if(redeemable < out) return {0, out_sym};
        }

        return out;
    }
}