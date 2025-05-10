## CPU overhead measurement

This script will：
- start the image registry 
- start the host CPU and DPU programs of Poby
- use the CLI to simulate the process of pulling 
- sample the host CPU overhead during this process 
    - In our experiment, we restrict Poby to run on host CPU 5. Therefore, the script samples CPU overhead by monitoring utilization of host CPU 5.


The results will be written to the `log/cpuoverhead/` directory.


```shell
cd /path/to/poby

./scripts/cpuoverhead/cpuoverhead.sh
```