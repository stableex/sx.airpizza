#pragma once

#include <eosio/asset.hpp>

namespace airpizza {

    using namespace eosio;

    const name id = "airpizza"_n;
    const name code = "air.pizza"_n;
    const std::string description = "Air.Pizza Converter";

    const extended_symbol USDT { symbol{"USDT",4}, "tethertether"_n };

    struct market_config {
        uint32_t    leverage;
        float_t     fee_rate;
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

    static int64_t normalize( const asset in, const uint8_t precision)
    {
        check(precision >= in.symbol.precision(), "ecurve::normalize: invalid normalize precision");
        const int64_t res = in.amount * static_cast<int64_t>( pow(10, precision - in.symbol.precision() ));
        check(res >= 0, "ecurve::normalize: overflow");

        return res;
    }

    static asset denormalize( const int64_t amount, const uint8_t precision, const symbol sym)
    {
        check(precision >= sym.precision(), "ecurve::denormalize: invalid precision");
        return asset{ amount / static_cast<int64_t>(pow( 10, precision - sym.precision() )), sym };
    }

    /**
     * ## STATIC `get_amount_out`
     *
     * Given an input amount of an asset and pair id, returns the calculated return
     * Based on Curve.fi formula (with a tweak): https://www.curve.fi/stableswap-paper.pdf
     *
     * ### params
     *
     * - `{asset} in` - input amount
     * - `{symbol} out_sym` - out symbol
     *
     * ### example
     *
     * ```c++
     * // Inputs
     * const asset in = asset { 10000, "USDT" };
     * const symbol out_sym = symbol { "DAI,6" };
     *
     * // Calculation
     * const asset out = ecurve::get_amount_out( in, out_sym );
     * // => 0.999612
     * ```
     */
    static asset get_amount_out( const asset quantity, const symbol out_sym, const symbol lptoken )
    {
        check(quantity.amount > 0, "airpizza: INSUFFICIENT_INPUT_AMOUNT");
        check(lptoken.is_valid(), "airpizza: Invalid liquidity token");

        market _market(code, code.value);
        const auto pool = _market.get(lptoken.code().raw(), "airpizza: Can't find market");
        check(pool.reserves.size() == 2, "airpizza: Only 2-reserve pools supported");

        const int128_t A = pool.config.leverage;
        const double fee = 0.0005;//pool.config.fee_rate;
        auto res_in = pool.reserves[0];
        auto res_out = pool.reserves[1];
        if(res_in.symbol != quantity.symbol) std::swap(res_in, res_out);
        print("\nReserves: ", res_in, ", ", res_out);
        print("\nA: ", A);
        check(res_in.symbol == quantity.symbol && res_out.symbol == out_sym, "airpizza: wrong pool");
        check(res_in.amount > 0 && res_out.amount > 0, "airpizza: Empty reserves");
        uint8_t precision = max(res_in.symbol.precision(), res_out.symbol.precision());

        const auto reserve_in = normalize(res_in, precision);
        const auto reserve_out = normalize(res_out, precision);
        const auto amount_in = normalize(quantity, precision);
        print("\nNormalized Reserves: ", reserve_in, ", ", reserve_out);
        print("\nNormalized amount_in: ", amount_in);

        const uint64_t sum = reserve_in + reserve_out;
        uint128_t D = sum, D_prev = 0;
        int i = 10;
        while ( D != D_prev && i--) {
            print("\nD: ", D);
            uint128_t prod1 = D * D / (reserve_in * 2) * D / (reserve_out * 2);
            D_prev = D;
            D = 2 * D * (A * sum + prod1) / ((2 * A - 1) * D + 3 * prod1);
        }
        print("\nD final: ", D);

        const int128_t b = (int128_t) ((reserve_in + amount_in) + (D / (A * 2))) - (int128_t) D;
        const uint128_t c = D * D / ((reserve_in + amount_in) * 2) * D / (A * 4);
        uint128_t x = D, x_prev = 0;
        i = 10;
        while ( x != x_prev && i--) {
            print("\nx: ", x);
            x_prev = x;
            x = (x * x + c) / (2 * x + b);
        }
        print("\nx final: ", x);
        uint64_t amount_out = reserve_out - (int64_t)x;


        print("\namount_out: ", amount_out, ", fee: ", fee);

        amount_out -= fee * amount_out / 10000;
        check(amount_out > 0, "airpizza: non-positive OUT");

        //check(false, "hi");

        return denormalize( amount_out, precision, out_sym );
    }
}