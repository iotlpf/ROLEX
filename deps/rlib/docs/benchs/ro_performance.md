# Benchmark Performance

## Table of Contents

* [RO Basic](#ro_basic)
* [RO Flying request](#ro_fly)
* [RO Outstanding request](#ro_or)
* [RO Doorbell Batching](#ro_db)
* [RO Coroutine](#ro_co)


<a  name="ro_basic"></a>

## RO Basic

```bash
$make bench_client
$./bench_client -client_name val08 -threads 12
```


   |          Setup          | per-thread | peek    |
   | ----------------------- | ---------- | ------- |
   | 1 clients * 12 threads  | 0.48M      | 5.8M    |
   | 2 clients * 12 threads  | 0.475M     | 11.4 M  |
   | 3 clients * 12 threads  | 0.467M     | 16.8M   |


<a  name="ro_fly"></a>

## RO Flying request

```bash
$make fly_client
$./fly_client -client_name val08 -threads 12
```

   |          Setup          | per-thread | peek    |
   | ----------------------- | ---------- | ------- |
   | 1 clients * 12 threads  | 2.5M       | 30M     |
   | 2 clients * 12 threads  | 2.5M       | 60M     |
   | 3 clients * 12 threads  | 2.5M       | 90M     |

<a  name="ro_or"></a>

## RO Outstanding request

```bash
$make or_client
$./or_client -client_name val08 -threads 12
```

   |          Setup          | per-thread | peek    |
   | ----------------------- | ---------- | ------- |
   | 1 clients * 12 threads  | 3.67M      | 44M     |
   | 2 clients * 12 threads  | 3.62M      | 87M     |
   | 3 clients * 12 threads  | 3.44M      | 124M    |

<a  name="ro_db"></a>

## RO Doorbell Batching

```bash
$make db_client
$./db_client -client_name val08 -threads 12
```

   |          Setup          | per-thread | peek    |
   | ----------------------- | ---------- | ------- |
   | 1 clients * 12 threads  | 5.5M       | 66M     |
   | 2 clients * 12 threads  | 5.5M       | 132M    |
   | 3 clients * 12 threads  | 3.75M      | 135M    |


<a  name="ro_co"></a>

## RO Coroutine

```bash
$cd r2
$make co_client
$./co_client -client_name val08 -threads 12 -coroutines 10
```

   |          Setup          | per-thread | peek   |
   | ----------------------- | ---------- | ------ |
   | 1 clients * 12 threads  | 2.25M      | 27M    |
   | 2 clients * 12 threads  | 2.25M      | 54M    |
   | 3 clients * 12 threads  | 2.25M      | 81M    |