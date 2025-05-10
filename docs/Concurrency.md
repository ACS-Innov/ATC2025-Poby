## Concurrency

This script will：
- start the image registry 
- start the host CPU and DPU programs of Poby
- use the CLI to simulate the process of pulling different images under different concurrency

The results will be written to the `log/concurr/` directory.


```shell
cd /path/to/poby

./scripts/concurr/concurr.sh
```
