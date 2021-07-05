# Iono Pi kernel module

Kernel module for using [Iono Pi](https://www.sferalabs.cc/iono-pi/), the Raspberry Pi based industrial PLC, via sysfs files.

For example, from the shell:

Toggle a relay:

    $ echo F > /sys/class/ionopi/relay/o1
    
Read the voltage on AI1:

    $ cat /sys/class/ionopi/analog_in/ai1_mv
    
Or using Python:

    f = open('/sys/class/ionopi/relay/o1', 'w')
    f.write('F')
    f.close()
    print('Relay O1 switched')

    f = open('/sys/class/ionopi/analog_in/ai1_mv', 'r')
    val = f.read().strip()
    f.close()
    print('AI1: ' + val + ' mv')


## Compile and Install

If you don't have git installed:

    sudo apt install git

Clone this repo:

    git clone --depth 1 https://github.com/sfera-labs/iono-pi-kernel-module.git
    
Install the Raspberry Pi kernel headers:

    sudo apt install raspberrypi-kernel-headers

Make and install:

    cd iono-pi-kernel-module
    make
    sudo make install
    
Compile the Device Tree and install it:

    dtc -@ -Hepapr -I dts -O dtb -o ionopi.dtbo ionopi.dts
    sudo cp ionopi.dtbo /boot/overlays/
    
Add to `/boot/config.txt` the following line:

    dtoverlay=ionopi
    
If you want to use TTL1 as 1-Wire bus, add this line too:

    dtoverlay=w1-gpio

Optionally, to be able to use the `/sys/class/ionopi/` files not as super user, create a new group "ionopi" and set it as the module owner group by adding an udev rule:

    sudo groupadd ionopi
    sudo cp 99-ionopi.rules /etc/udev/rules.d/

and add your user to the group, e.g., for user "pi":

    sudo usermod -a -G ionopi pi

Reboot:

    sudo reboot

## Usage

After installation, you will find all the available devices under the directory `/sys/class/ionopi/`.

The following paragraphs list all the possible devices (directories) and files coresponding to Iono Pi's features. 

You can read and/or write to these files to configure, monitor and control your Iono Pi.

### Analog Inputs - `/sys/class/ionopi/analog_in/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|ai&lt;N&gt;_mv|R|&lt;val&gt;|Voltage value read on AI&lt;N&gt; in mV|
|ai&lt;N&gt;_raw|R|&lt;val&gt;|Raw value read from the ADC channel connected to AI&lt;N&gt; not converted|

Examples:

    cat /sys/class/ionopi/analog_in/ai1_mv
    cat /sys/class/ionopi/analog_in/ai2_raw

### Digital Inputs - `/sys/class/ionopi/digital_in/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|di&lt;N&gt;|R|1|Digital input &lt;N&gt; high|
|di&lt;N&gt;|R|0|Digital input &lt;N&gt; low|

For each digital input, we also expose: 
* the debounced state
* 2 debounce times in ms ("on" for high state and "off" for low state) with default value of 50ms
* 2 state counters ("on" for high state and "off" for low state)

The debounce times for each DI has been splitted in "on" and "off" in order to make the debounce feature more versatile and suited for particular application needs (e.g. if we consider digital input 1, and set its debounce "on" time to 50ms and its debounce "off" time to 0ms, we just created a delay-on type control for digital input 1 with delay-on time equal to 50ms).    
Change in value of a debounce time automatically resets both counters.    
The debounce state of each digital input at system start is UNDEFINED (-1), because if the signal on the specific channel cannot remain stable for a period of time greater than the ones defined as debounce "on" and "off" times, we are not able to provide a valid result. 

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|di*N*_deb|R|1|Digital input *N* debounced value high|
|di*N*_deb|R|0|Digital input *N* debounced value low|
|di*N*_deb|R|-1|Digital input *N* debounced value undefined|
|di*N*_deb_on_ms|RW|val|Minimum stable time in ms to trigger change of the debounced value of digital input *N* to high state. Default value=50|
|di*N*_deb_off_ms|RW|val|Minimum stable time in ms to trigger change of the debounced value of digital input *N* to low state. Default value=50|
|di*N*_deb_on_cnt|R|val| Number of times with the debounced value of the digital input *N* in high state. Rolls back to 0 after 4294967295|
|di*N*_deb_off_cnt|R|val|Number of times with the debounced value of the digital input *N* in low state. Rolls back to 0 after 4294967295|

### LED - `/sys/class/ionopi/led/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|status|R/W|0|LED off|
|status|R/W|1|LED on|
|status|W|F|Flip LED's state|
|blink|W|&lt;t&gt;|LED on for &lt;t&gt; ms|
|blink|W|&lt;t_on&gt; &lt;t_off&gt; &lt;rep&gt;|LED blink &lt;rep&gt; times with &lt;t_on&gt;/&lt;t_off&gt; ms periods. E.g. "200 50 3"|

### Relays - `/sys/class/ionopi/relay/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|o&lt;N&gt;|R/W|0|Relay O&lt;N&gt; open|
|o&lt;N&gt;|R/W|1|Relay O&lt;N&gt; closed|
|o&lt;N&gt;|W|F|Flip relay O&lt;N&gt;'s state|
    
### Open collectors - `/sys/class/ionopi/open_coll/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|oc&lt;N&gt;|R/W|0|Open collector OC&lt;N&gt; open|
|oc&lt;N&gt;|R/W|1|Open collector OC&lt;N&gt; closed|
|oc&lt;N&gt;|W|F|Flip open collector OC&lt;N&gt;'s state|

### Wiegand - `/sys/class/ionopi/wiegand/`

You can use the TTL lines as Wiegand interfaces for keypads or card reader. You can connect up to two Wiegand devices using TTL1/TTL2 respctively fot the D0/D1 lines of the first device (w1) and TTL3/TTL4 for D0/D1 of the second device (w2).

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|w&lt;N&gt;_enabled|R/W|0|Wiegand interface w&lt;N&gt; disabled|
|w&lt;N&gt;_enabled|R/W|1|Wiegand interface w&lt;N&gt; enabled|
|w&lt;N&gt;_data|R|&lt;ts&gt; &lt;bits&gt; &lt;data&gt;|Latest data read from wiegand interface w&lt;N&gt;. The first number (&lt;ts&gt;) represents an internal timestamp of the received data, it shall be used only to discern newly available data from the previous one. &lt;bits&gt; reports the number of bits received (max 64). &lt;data&gt; is the sequence of bits received represnted as unsigned integer|

The following parameters can be used to improve noise filtering:

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|w&lt;N&gt;_pulse_width_max|R/W|&lt;val&gt;|Maximum bit pulse width in &micro;s|
|w&lt;N&gt;_pulse_width_min|R/W|&lt;val&gt;|Minimum bit pulse width in &micro;s|
|w&lt;N&gt;_pulse_itvl_max|R/W|&lt;val&gt;|Maximum interval between pulses in &micro;s|
|w&lt;N&gt;_pulse_itvl_min|R/W|&lt;val&gt;|Minimum interval between pulses in &micro;s|

### Secure Element - `/sys/class/ionopi/sec_elem/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|serial_num|R|9 1-byte HEX values|Secure element serial number|

### 1-Wire - `/sys/bus/w1/devices/`

You will find the list of connected 1-Wire sensors' IDs in `/sys/bus/w1/devices/` with format `28-XXXXXXXXXXXX`.
To get the measured temperature read the file `w1_slave` under the sensor's directory, e.g.:

    $ cat /sys/bus/w1/devices/28-XXXXXXXXXXXX/w1_slave 
    25 01 55 00 7f ff 0c 0c 08 : crc=08 YES
    25 01 55 00 7f ff 0c 0c 08 t=18312
    
At the end of the first line you will read `YES` if the communication succeded and the read temperature value will be reported at the end of the second line expressed in &deg;C/1000.
