RLib provides several new abstractions for easier use of RDMA.
It is a header-only library with minimal dependices, i.e., `ibverbs`. 

Please find examples in `./examples` for how to use RLib. 
For detailed documentations or benchmark results (including code and scripts),
please check `docs`. 

## Example

Here is a simplified example of using RLib to implement an one-sided READ:
```c++
      Arc<RC> qp; // some pre-initialized QP

      // An example of using Op to post an one-sided RDMA read.
      ::rdmaio::qp::Op op;
      op.set_rdma_rbuf(rmr.buf + 0xc, rmr.key).set_read();
      op.set_payload((u64)(lmr.buf), sizeof(u64), lmr.key)

      // post the requests
      auto ret = op.execute(qp, IBV_SEND_SIGNALED);
      RDMA_ASSERT(ret == IOCode::Ok);
    
     // wait for the completion
     auto res = qp->wait_one_comp();
     RDMA_ASSERT(res == IOCode::Ok) << "req error: " << res.desc;
     
```

For more examples, please check the `examples` folder. 
There is a description of the examples in `docs/examples/` .

## License

RLib is released under the [Apache 2.0 license](http://www.apache.org/licenses/LICENSE-2.0.html).

As RLib is the refined execution framework of DrTM+H,
if you use RLib in your research, please cite our paper:

    @inproceedings {drtmhosdi18,
        author = {Xingda Wei and Zhiyuan Dong and Rong Chen and Haibo Chen},
        title = {Deconstructing RDMA-enabled Distributed Transactions: Hybrid is Better!},
        booktitle = {13th {USENIX} Symposium on Operating Systems Design and Implementation ({OSDI} 18)},
        year = {2018},
        isbn = {978-1-939133-08-3},
        address = {Carlsbad, CA},
        pages = {233--251},
        url = {https://www.usenix.org/conference/osdi18/presentation/wei},
        publisher = {{USENIX} Association},
        month = oct,
    }
