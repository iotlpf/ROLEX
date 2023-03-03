R2 is a communication library over RDMA, which aimes at fast and easy use of RDMA.
It is built over `ibverbs`, with minimal dependencies. 
It provides several new abstractions for easier use of RDMA, 
such as high-performance messaging primitives, lightweight async one-sided RDMA operations 
using corotuines.
R2 is well-tuned to achieved the best-possible performance with minimal overhead due to better abstractions. 
It is part of **DrTM+H**, a high performance experimantal transactional engine using RDMA.


## License

R2 is released under the [Apache 2.0 license](http://www.apache.org/licenses/LICENSE-2.0.html).

As R2 is the refined execution framework of DrTM+H, 
if you use R2 in your research, please cite our paper:

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
