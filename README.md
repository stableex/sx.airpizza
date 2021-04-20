# **`Air.pizza`**

> Peripheral EOSIO smart contracts for interacting with `air.pizza` smart contract


## Quickstart

```c++
#include <sx.airpizza/airpizza.hpp>

// user input
const asset quantity = asset{100000, symbol{"USDT", 4}};
const symbol out_sym = symbol{"USDE", 4};
const symbol_code lptoken = symbol_code{ "USDI" }; // USDT/USDE pair

// calculate out price
const asset out = airpizza::get_amount_out( quantity, out_sym, lptoken);
// => "9.6500 USDE"
```
