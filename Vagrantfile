# All Vagrant configuration is done below. The "2" in Vagrant.configure
# configures the configuration version (we support older styles for
# backwards compatibility). Please don't change it unless you know what
# you're doing.
Vagrant.configure("2") do |config|
  config.vm.box = "centos/7"

  config.vm.provider "virtualbox" do |vb|
    # Display the VirtualBox GUI when booting the machine
    # vb.gui = true  

    vb.memory = "2048"
    vb.cpus = 2
  end

  config.vm.provision "shell", inline: <<-SHELL
	yum install git gcc emacs https://centos7.iuscommunity.org/ius-release.rpm -y
	git clone https://github.com/Babtsov/jos.git
	chown -R vagrant:vagrant jos
	yum install jos/cookbooks/mit_patched_qemu.rpm -y
  SHELL

end
