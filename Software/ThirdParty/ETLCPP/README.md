# Embedded Template Library (ETL)

Headers only, taken from https://github.com/ETLCPP/etl at tag **20.42.1**,
MIT licensed (see `LICENSE`). Only `include/` is vendored; the upstream
tests, examples and CMake packaging are not.

Added on the `pilot/etl-fixed-containers` branch to evaluate fixed-capacity
containers against the `std::` ones in kernel code. Nothing outside
`Software/Libs/.../SensorLayer/Core/SensorManager.*` uses it yet.
