# Memory configuration

This script will：
- start the image registry under different memory block size
- start the host CPU and DPU programs of Poby under different memory block size and different numbers of memory block
- use the CLI to simulate the process of pulling 


The results will be written to the `log/mem_block/` directory.

You need to check and change the `-content_server_registry_path` in `scripts/mem_block/start_poby.sh` because images of different block size store in different folders.

```shell
cd /path/to/poby

./scripts/mem_block/mem_block.sh
```
