## End to end performance

This script will：
- start the image registry 
- start the host CPU and DPU programs of Poby
- use the CLI to simulate the process of pulling different images

The results will be written to the `log/e2e/` directory.

```shell
cd /path/to/poby

# run poby
./scripts/e2e/poby.sh

# run baselines
./scripts/e2e/containerd_baseline.sh
./scripts/e2e/iSulad_baseline.sh
```
