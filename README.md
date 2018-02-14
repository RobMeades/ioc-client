Load Raspbian
=============
First, load Raspbian into your Raspberry Pi.  I used the minimal image, no desktop, and once I'd created the SD card I also created an empty file on the `boot` drive called `SSH` (all in caps, no extension); this switches on SSH so, provided you can determine what IP address the Pi has been allocated, you can do everything else from an SSH terminal (default username `pi` and default password `raspberry`).

Configure I2S
=============
Configuration of the I2S interface on the Raspberry Pi is based on the instructions that can be found here:

http://www.raspberrypi.org/forums/viewtopic.php?f=44&t=91237

Edit `/boot/config.txt` to make sure that the following two lines are not commented-out:

```
dtparam=audio=on
dtparam=i2s=on
```
Build And Load The ICS43432 Microphone Driver
=============================================
Next we need to build and load the ICS43432 microphone driver.  This is already available as part of the Linux source tree but is not built or loaded by default.

These steps are based on this blog post:

https://www.raspberrypi.org/forums/viewtopic.php?t=173640

Install a few required utilities:

```
sudo apt-get install bc
sudo apt-get install libncurses5-dev
```
Download the correct source version for your kernel and clean it up using the `rpi-source` utility as follows:

```
sudo wget https://raw.githubusercontent.com/notro/rpi-source/master/rpi-source -O /usr/bin/rpi-source
sudo chmod +x /usr/bin/rpi-source
/usr/bin/rpi-source -q --tag-update
rpi-source
```
If you receive an error like this:

`gcc version check: mismatch between gcc (6) and /proc/version (4.9.3)`

...where the GCC version is higher than the `/proc/version` version, then that's OK, just run `rpi-source` again with the parameter `--skip-gcc`.  `rpi-source` may ask you to install other things along the way; do what it says.  When it has completed, amongst other things, you should now have the following file:

`~/linux/sound/soc/codecs/ics43432.c`

Change to the Linux directory:

`cd linux`

Make a backup of your current `.config` file with:

`cp .config back.config`

Now run:

`make menuconfig`

Use the arrow keys to navigate down the menu tree as follows:

`Device drivers ---> Sound card support ---> Advance Linux Sound Architecture ---> ALSA for SoC audio support ---> CODEC drivers ---> InvenSense ICS43432 I2S microphone codec`

...and press the space bar to put an `m` against that entry.  Exit by pressing ESC lots of times, saving the `.config` file when prompted.

Now include this change and build the codecs module with:

```
make prepare
make M=sound/soc/codecs
```
Look in the `sound/soc/codecs` directory again and you should now see the file `snd-soc-ics43432.ko`.

Try adding the module manually with:

`sudo insmod sound/soc/codecs/snd-soc-ics43432.ko`

If this fails with the error `Could not insert module, invalid module format` then you've got the wrong version of Linux kernel source for the Linux binary you are using and you need to repeat this section.

NOTE: when you do `sudo apt-get upgrade` this might happen again if the Linux version changes as a result.  You can check the Linux version using `uname -r`; if the version has changed you need to re-download the Linux source with `rpi-source` and repeat the actions of this section to build a compatible `snd-soc-ics43432.ko` file.

Install the module with:

``sudo cp sound/soc/codecs/snd-soc-ics43432.ko /lib/modules/`uname -r`/``

Run `sudo depmod` so as to let Linux work out the dependencies.

Finally, to load the module at boot, edit the file `/etc/modules` to add the line:

`snd-soc-ics43432`

Reboot and use `lsmod` to check that the driver has been automatically loaded.  The output should look something like this:

```
Module                  Size  Used by
cfg80211              544545  0
rfkill                 20851  2 cfg80211
snd_bcm2835            24427  0
snd_soc_bcm2835_i2s     7480  0
bcm2835_gpiomem         3940  0
uio_pdrv_genirq         3923  0
uio                    10204  1 uio_pdrv_genirq
fixed                   3285  0
snd_soc_ics43432        2287  0
snd_soc_core          180471  2 snd_soc_ics43432,snd_soc_bcm2835_i2s
snd_compress           10384  1 snd_soc_core
snd_pcm_dmaengine       5894  1 snd_soc_core
snd_pcm                98501  4 snd_pcm_dmaengine,snd_soc_bcm2835_i2s,snd_bcm2835,snd_soc_core
snd_timer              23968  1 snd_pcm
snd                    70032  5 snd_compress,snd_timer,snd_bcm2835,snd_soc_core,snd_pcm
ip_tables              13161  0
x_tables               20578  1 ip_tables
ipv6                  408900  24
```

If `snd_soc_ics43432` does not appear in the list, check what went wrong during boot using `journalctl -b` and/or `dmesg`.

Now we need to create a device tree entry to make use of this driver.  Create a file `i2s-soundcard-overlay.dts` with this content:

```
/dts-v1/;
/plugin/;

/ {
    compatible = "brcm,bcm2708";

    fragment@0 {
        target = <&i2s>;
        __overlay__ {
            status = "okay";
        };
    };

    fragment@1 {
        target-path = "/";
        __overlay__ {
            card_codec: card-codec {
                #sound-dai-cells = <0>;
                compatible = "invensense,ics43432";
                status = "okay";
            };
        };
    };

    fragment@2 {
        target = <&sound>;
        master_overlay: __dormant__ {
            compatible = "simple-audio-card";
            simple-audio-card,format = "i2s";
            simple-audio-card,name = "soundcard";
            simple-audio-card,bitclock-master = <&dailink0_master>;
            simple-audio-card,frame-master = <&dailink0_master>;
            status = "okay";
            simple-audio-card,cpu {
                sound-dai = <&i2s>;
            };
            dailink0_master: simple-audio-card,codec {
                sound-dai = <&card_codec>;
            };
        };
    };

    fragment@3 {
        target = <&sound>;
        slave_overlay: __overlay__ {
                compatible = "simple-audio-card";
                simple-audio-card,format = "i2s";
                simple-audio-card,name = "soundcard";
                status = "okay";
                simple-audio-card,cpu {
                    sound-dai = <&i2s>;
                };
                dailink0_slave: simple-audio-card,codec {
                    sound-dai = <&card_codec>;
                };
        };
    };

    __overrides__ {
        alsaname = <&master_overlay>,"simple-audio-card,name",
                    <&slave_overlay>,"simple-audio-card,name";
        compatible = <&card_codec>,"compatible";
        master = <0>,"=2!3";
    };
};
```
Compile and install this as follows:

```
dtc -@ -I dts -O dtb -o i2s-soundcard.dtbo i2s-soundcard-overlay.dts
sudo cp i2s-soundcard.dtbo /boot/overlays
```
[Note: ignore the warnings that the dtc compilation process throws up].

Finally, edit the file `/boot/config.txt` to append the line:

`dtoverlay=i2s-soundcard,alsaname=mems-mic`

Now reboot and then check for sound cards with:

`arecord -l`

...and you should see:

```
**** List of CAPTURE Hardware Devices ****
card 1: memsmic [mems-mic], device 0: bcm2835-i2s-ics43432-hifi ics43432-hifi-0 []
  Subdevices: 1/1
  Subdevice #0: subdevice #0
```
The output from `lsmod` should now look something like this:

```
Module                  Size  Used by
cfg80211              544545  0
rfkill                 20851  2 cfg80211
snd_soc_simple_card     6297  0
snd_soc_simple_card_utils     5196  1 snd_soc_simple_card
snd_bcm2835            24427  0
bcm2835_gpiomem         3940  0
snd_soc_bcm2835_i2s     7480  2
uio_pdrv_genirq         3923  0
fixed                   3285  0
uio                    10204  1 uio_pdrv_genirq
snd_soc_ics43432        2287  1
snd_soc_core          180471  4 snd_soc_ics43432,snd_soc_simple_card_utils,snd_soc_bcm2835_i2s,snd_soc_simple_card
snd_compress           10384  1 snd_soc_core
snd_pcm_dmaengine       5894  1 snd_soc_core
snd_pcm                98501  4 snd_pcm_dmaengine,snd_soc_bcm2835_i2s,snd_bcm2835,snd_soc_core
snd_timer              23968  1 snd_pcm
snd                    70032  5 snd_compress,snd_timer,snd_bcm2835,snd_soc_core,snd_pcm
ip_tables              13161  0
x_tables               20578  1 ip_tables
ipv6                  408900  24
```
Connect An ICS43432 MEMS Microphone
===================================
The pins you need on the Raspberry Pi header are as follows:

* Pin 12: I2S clock
* Pin 35: I2S frame
* Pin 38: I2S data in
* Pin 39: ground
* Pin 1:  3.3V
* [Pin 40: I2S data out]

If you want to confirm that all is good, attach an oscilloscope or logic analyser to the pins and activate the pins by requesting a 10 second long recording with:

`arecord -Dhw:1 -c2 -r16000 -fS32_LE -twav -d10 -Vstereo test.wav`

You should see something like this:

![analyser trace 1](pi_i2s_capture_1.png "Analyser Trace of I2S CLK/FS")

Connect up your ICS43432 MEMS microphone, with the LR select pin grounded, and you should see data flowing something like this:

![analyser trace 2](pi_i2s_capture_2.png "Analyser Trace of I2S CLK/FS/Data")

If you touch the microphone while the recording is running you should see the VU meter displayed in the SSH window change.

To make a proper capture you will need to configure for a mono microphone and a sensible recording level.  In your home directory, create a file called `.asoundrc` with the following contents:

```
pcm.mic_hw {
    type hw
    card memsmic
    channels 2
    format S32_LE
}
pcm.mic_sv {
    type softvol
    slave.pcm mic_hw
    control {
        name "Boost Capture Volume"
        card memsmic
    }
    min_dB -3.0
    max_dB 50.0
}
pcm.mic_mono {
    type multi
    slaves.a.pcm mic_sv
    slaves.a.channels 2
    bindings.0.slave a
    bindings.0.channel 0
}
```
Check that your configuration is correct by making a recording with this newly defined device:

`arecord -Dmic_sv -c2 -r16000 -fS32_LE -twav -d10 -Vstereo test.wav`

Now run `alsamixer`, call up the sound card menu by pressing `F6`, select `mems-mic` and then press the `TAB` key and set the Boost capture volume level (you can try using `F4` instead but that is often grabbed by the terminal program and hence may do other things).  Use the arrow keys to set a Boost of around 30 dB and press `ESC` to exit.

Now run another recording and, hopefully, you will get a better sound level in your `test.wav` file.  Finally, to get a mono recording, use:

`arecord -Dmic_mono -r16000 -fS32_LE -twav -d10 -Vmono test.wav`

Remote Access
=============
I set up the Raspberry Pi to use a DDSN account at www.noip.com so that I can get to it remotely.  Do this by configuring a DDNS end point for the Raspberry Pi in your www.noip.com account.  Then download and build the Linux update client on the Raspberry Pi as follows:

```
wget https://www.noip.com/client/linux/noip-duc-linux.tar.gz
tar xzf noip-duc-linux.tar.gz
cd noip-2.1.9-1/
make
sudo make install
```
You will need to supply your www.noip.com account details and chose the correct DDNS entry to link to the Raspberry Pi.

Set permissions correctly with:

```
sudo chmod 700 /usr/local/bin/noip2
sudo chown root:root /usr/local/bin/noip2
sudo chmod 600 /usr/local/etc/no-ip2.conf
sudo chown root:root /usr/local/etc/no-ip2.conf
```
Create a file named `noip.service` in the `/etc/systemd/system/` directory with the following contents:

```
[Unit]
Description=No-ip.com dynamic IP address updater
After=network-online.target
After=syslog.target

[Install]
WantedBy=multi-user.target
Alias=noip.service

[Service]
# Start main service
ExecStart=/usr/local/bin/noip2
Restart=always
Type=forking
```
Check that the `noip` daemon starts correctly with:

`sudo systemctl start noip`

Your www.noip.com account should show that the update client has been in contact.  Reboot and check that the service has been started automatically with:

`sudo systemctl status noip`

...and by checking once more that your www.noip.com account shows that the update client has been in contact.

Developing With ALSA
====================
This code is linked against the ALSA libraries so you'll need the ALSA development package.  Get this with:

`sudo apt-get install libasound2-dev`

Using A USB Modem
=================
Install `minicom` with:

`sudo apt-get install minicom`

Plug in your USB modem into one of the Raspberry Pi's USB ports.  I used a Hologram USB stick with a u-blox USB modem, which is a pure modem and requires no `usb-modeswitch` messing about.  I created a persistent device name for your USB stick using `udev`. With the USB stick plugged in, enter:

`lsusb`

Find the entry for your device.  Mine was:

`Bus 001 Device 006: ID 1546:1102 U-Blox AG`

Create a file `90-ioc.rules` in `/etc/udev/rules.d` with the following contents:

`ACTION=="add", KERNEL=="tty*", ATTRS{idVendor}=="1546", ATTRS{idProduct}=="1102", SYMLINK+="modem"`

...adjusting the numbers as necessary for your modem and making sure there is a newline at the end.  Reboot and check that `/dev` now includes the device `modem`.  If it doesn't, you can check if there are any errors reading the new rule with:

`udevadm test /dev/bus/001/006`

...replacing the values 001 and 006 as appropriate for your device.

Note: for reasons I don't understand, `minicom` and `wvdial` didn't play will with my `/dev/modem` device: they seemed to lock it (with a lock file in `/var/lock/`, then be unable to use it but leave it locked.  So you will see below that I continued to use `/dev/ttyACM0` with those applications.  The rules file, though, is still required, see later on.

Run `minicom` with:

`minicom -b115200 -D/dev/ttyACM0`

...and check that typing `AT` gets the response `OK`, just to confirm that your modem is talking to the Raspberry Pi.  Exit `minicom` with CTRL-A X

Now install PPP and a dialler with:

`sudo apt-get install ppp wvdial`

Edit the file `/etc/wvdial.conf` for your modem.  For my u-blox modem (on a Hologram USB stick with a Hologram SIM) I used:

```
[Dialer Defaults]
Init1 = ATZ
Init2 = ATE0 +CMEE=2
Init3 = AT&C1 &D2
Init4 = AT+CGDCONT=1, "IP", "hologram"
Init5 = AT+IPR=460800
Modem Type = USB Modem
Baud = 460800
New PPPD = yes
Modem = /dev/ttyACM0
ISDN = 0
Phone = *99***1#
Password = "blank"
Username = "blank"
```
Test this with:

`sudo wvdial &`

You should see the AT commands go past, all followed by nice `OK`'s from the modem, then PPP should negotiate the connection, something like this:

```
--> Carrier detected.  Waiting for prompt.
~[7f]}#@!}!}!} }4}"}&} } } } }%}&MJJI}'}"}(}"C[19]~
--> PPP negotiation detected.
--> Starting pppd at Thu Feb  8 21:05:49 2018
--> Pid of pppd: 848
--> Using interface ppp0
--> pppd: ▒[07]R
--> pppd: ▒[07]R
--> pppd: ▒[07]R
--> local  IP address 10.170.210.39
--> pppd: ▒[07]R
--> remote IP address 10.170.210.39
--> pppd: ▒[07]R
--> primary   DNS address 212.9.0.135
--> pppd: ▒[07]R
--> secondary DNS address 212.9.0.136
--> pppd: ▒[07]R
```
...and the screen should stop scrolling.  Press `<enter>` to get back to the command prompt and type `ifconfig`.  You should now have a `ppp0` connection as well as the usual `eth0` etc.  To disconnect the PPP link and stop running-up your cellular bill, enter:

`ps aux | grep wvdial`

You'll get something like:

```
root       968  0.1  0.3   7228  3444 pts/0    S    21:10   0:00 sudo wvdial
root       972  0.2  0.4  10588  4240 pts/0    S    21:10   0:00 wvdial
root       973  0.0  0.2   3996  2068 pts/0    S    21:10   0:00 /usr/sbin/pppd 460800 modem crtscts defaultroute usehostname -detach user blank noipdefault call wvdial usepeerdns idle 0 logfd 6
pi        1036  0.0  0.0   4372   576 pts/0    S+   21:10   0:00 grep --color=auto wvdial
```
Find the task number against the line `sudo wvdial` and kill that task; in my case:

`sudo kill 968`

You now have proven cellular connectivity.

Web Server Setup
================
Install `nginx` with:

`sudo apt-get install nginx`

Enter the local IP address of your Raspberry Pi into a browser and you should see the default `nginx` page with `Welcome to nginx!` on the top in large friendly letters.

Connecting To A Server
======================
Before completing this section you will need to set up the server-side of the IOC, for which see <insert link here>.

Generate a key pair:

`ssh-keygen -f ~/ioc -t ecdsa -b 521`

Don't add a passphrase as we will need the Raspberry Pi to be able to use the key without manual passphrase entry.  Make sure the Raspberry Pi is on-line and copy the public key to the server with:

`ssh-copy-id -i ~/ioc user@host`

...replacing `user` with your username on the server and `host` with the IP address of the server.

Make sure that you can log in to the server from the Raspberry Pi using SSH and this key with:

`ssh -i ~/ioc user@host -p xxxx`

...again replacing `user` and `host` with the username and IP address for the server, and adding `-p xxxx` with the remote port number if it is not port 22.  If you have problems, try adding the `-vvv` switch to `ssh` to find out what it's up to while running `journalctl -f` on the server to determine what it is seeing.

Boot Setup
==========
To start up the cellular connection and open an SSH tunnel to the server at boot, you need to create a couple of services.  First create the file `/lib/systemd/system/cellular.service` with contents as follows:

```
[Unit]
Description=Cellular Connection
BindsTo=dev-modem.device
After=dev-modem.device
     
[Service]
ExecStart=wvdial
Restart=on-failure
RestartSec=3
StandardOutput=null
     
[Install]
WantedBy=multi-user.target
Alias=cellular.service
```
Note: if you have trouble with `wvdial`, change the ExecStart line to something like:

`ExecStart=wvdial > /home/pi/wvdial.log 2>&1`
 
...to get log output.
 
Edit the `/etc/udev/rules.d/90-ioc.rules` file you created above for the modem device so that it contains:

`ACTION=="add", KERNEL=="tty*", ATTRS{idVendor}=="1546", ATTRS{idProduct}=="1102", SYMLINK+="modem", TAG+="systemd", ENV{SYSTEMD_WANTS}="cellular.service"`

Test it with:

`sudo systemctl start cellular`

Your modem should connect to the cellular network and, if you run `ifconfig`, you should see the `ppp0` connection appear.  Shut it down again to save your cellular bill with:

`sudo systemctl stop cellular`

Create the file `/lib/systemd/system/ssh-tunnel.service` with contents as follows:

```
[Unit]
Description=SSH tunnel to server
Wants=network-online.target
After=network-online.target

[Service]
ExecStart=/usr/bin/ssh -o StrictHostKeyChecking=no -o "ConnectTimeout 10" -o "ServerAliveInterval 30" -o "ServerAliveCountMax 3" -N -L xxxx:localhost:yyyy -i /home/pi/ioc -p zzzz user@host

[Install]
WantedBy=multi-user.target
```
...replacing `user` with your username on the server, `host` with the IP address/URL of the server, using `-p zzzz` if SSH is not on port 22, replacing `xxxx` with the local port for the SSH tunnel and `yyyy` with the remote port on the server for the SSH tunnel.

Before you start the service, cut and paste your finalised `ExecStart` line and execute it on the command line directly with `sudo`.  This will add the finger-print of the server to the root account.

Now test that the tunnel comes up correctly with:

`sudo systemctl start ssh-tunnel`

Check on the server end (e.g. by running `journalctl -f`) that the connection is successful.  If you have trouble, add the `-vvv` switch to the `ssh` command line above, do a `sudo systemctl daemon-reload`, start the service again and look at the output on the Raspberry Pi with `journalctl -b`.

Enable the tunnel to start at boot with:

`sudo systemctl enable ssh-tunnel`

Reboot and check that the tunnel is open (this will be over Ethernet).  If that is successful, enable the cellular service to start at boot:

`sudo systemctl enable cellular`

Reboot without Ethernet/wifi connectivity from the Raspberry Pi and check that the tunnel opens over the cellular connection.  If you have any issues, use `systemctl status cellular` and `journalctl -b` to find out what's up.

FROM NOW ON YOUR CELLULAR MODEM WILL CONNECT AT BOOT AND YOU MUST RUN `sudo systemctl stop cellular` TO STOP IT.  If you want to be sure you don't waste money, disable this until you really need it with:

`sudo systemctl disable cellular`

...and of course run `sudo systemctl stop cellular` to stop the current instance.

SERVER SIDE
===========
Install `golang` with:

`sudo apt-get install golang-go`

Edit `/etc/profile` and add to it the following line:

`export PATH=$PATH:/usr/local/go/bin`

Install SSH with:

`sudo-apt-get install openssh-server`

...and make sure that you can log in using SSH from another machine with your username/password.

To protect the server from unauthorised users, make sure you have generated and installed key pairs according to the instructions for the ioc-client <insert link here>, then edit the file `/etc/ssh/sshd_config` and set `PasswordAuthentication` to `no`, then restart the `ssd` daemon with `sudo systemctl restart sshd`.

Debugging End To End Connectivity
==================================
If you find that the SSH tunnel won't connect or there are other end-to-end connectivity issues, try falling back to basic TCP testing with `netcat`.  On the server side run:

`netcat -v -l xxxx`

...where `xxxx` is the port number to listen on.  If the SSH server is active on port 22 then stop it first, or maybe just try a different port number to start with.  On the client side run:

`netcat -v host xxxx`

...where `host` is replaced by the address of the server and `xxxx` is the port.  If a connection is made, both ends will say so.  Try all of this initially with the Ethernet connection of the Raspberry Pi plugged in but bare in mind that if your server is on the same network then you aren't really testing things.  Maybe try running the client-side netcat line on another Linux server on the internet, just to be sure that the server is visible.

If this works from the command line, make sure it also works in the systemd unit files by replacing the line that invokes the SSH client with the netcat client-side line.