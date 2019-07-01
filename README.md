# MIT 6.828 Operating System Engineering
This repo contains all my solutions to the various exercises in MIT's 6.828 course

## Run the jos labs inside a virtual machine
You need to have `vagrant` installed on your local machine. The `Vagrantfile` contains a configuration of a centos virtual machine in which all the tools necessary to run jos and xv6 are installed (including MIT's modified version of qemu). 

To create and ssh into the VM, simply execute the following inside the `jos` directory:
```
vagrant up && vagrant ssh
```

After logging into the centos VM, you can can run jos of a particular lab, by executing the following:
```
cd ~/jos/lab6
make qemu-nox
```
press `ctrl+a x` to exit qemu.

