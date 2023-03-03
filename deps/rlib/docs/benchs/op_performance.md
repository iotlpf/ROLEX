# Benchmark Performance

> Doorbell batching reqeusts can easily get the peek of total throughput. 
Thus, we use it to evaluate RDMA performance with different operation types.

## Table of Contents

* [RO Doorbell Batching](#ro_db)
* [WO Doorbell Batching](#wo_db)
* [CAS Doorbell Batching](#cas_db)
* [FAA Doorbell Batching](#faa_db)


<a  name="ro_db"></a>

## RO Doorbell Batching

```bash
$make db_client
$./db_client -client_name val08 -threads 12 -op_type 0
```

   |          Setup          | per-thread | peek    |
   | ----------------------- | ---------- | ------- |
   | 1 clients * 12 threads  | 5.5M       | 66M     |
   | 2 clients * 12 threads  | 5.5M       | 132M    |
   | 3 clients * 12 threads  | 3.75M      | 135M    |


<a  name="wo_db"></a>

## WO Doorbell Batching

```bash
$make db_client
$./db_client -client_name val08 -threads 12 -op_type 1
```

   |          Setup          | per-thread | peek    |
   | ----------------------- | ---------- | ------- |
   | 1 clients * 12 threads  | 4.75M      | 57M     |
   | 2 clients * 12 threads  | 4.66M      | 112M    |
   | 3 clients * 12 threads  | 3.41M      | 123M    |


<a  name="cas_db"></a>

## CAS Doorbell Batching

```bash
$make db_client
$./db_client -client_name val08 -threads 12 -op_type 2
```

   |          Setup          | per-thread | peek    |
   | ----------------------- | ---------- | ------- |
   | 1 clients * 12 threads  | 2.08M      | 25M     |
   | 2 clients * 12 threads  | 1.83M      | 44M     |
   | 3 clients * 12 threads  | 1.33M      | 48M     |



<a  name="faa_db"></a>

## FAA Doorbell Batching

```bash
$make db_client
$./db_client -client_name val08 -threads 12 -op_type 3
```

   |          Setup          | per-thread | peek    |
   | ----------------------- | ---------- | ------- |
   | 1 clients * 12 threads  | 1.66M      | 20M     |
   | 2 clients * 12 threads  | 1.66M      | 40M     |
   | 3 clients * 12 threads  | 1.33M      | 48M     |