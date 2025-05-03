/*
  mitsubishi2Wifi Copyright (c) 2024 Smanar

  Based on mitsubishi2mqtt Copyright (c) 2022 gysmo38, dzungpv, shampeon, endeavour,
  jascdk, chrdavis, alekslyse. All rights reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.
  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.
  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

 
const char html_init_setup[] PROGMEM = R"====(
<div id='l1' name='l1'></div>
<fieldset>
    <legend><b>&nbsp; Initial setup &nbsp;</b></legend>
    <form method='post' action='save'>
        <p><b>Hostname</b>
            <br/>
            <input id='hn' name='hn' placeholder=' ' value='_UNIT_NAME_'>
        </p>
        <p><b>SSID</b> ()
            <br/>
            <input id='ssid' name='ssid' placeholder=' '>
        </p>
        <p><b>Password</b> ()
            <br/>
            <input id='psk' name='psk' placeholder=' '>
        </p>
        <p><b>OTA Password</b>
            <br/>
            <input id='otapwd' name='otapwd' placeholder=' '>
        </p>
        </p>
        <br/>
        <button name='submit' type='submit' class='button bgrn'> Save & Reboot </button>
    </form>
</fieldset>
<fieldset>
    <a class="button" href="/reboot" class="back"> Reboot </a>
</fieldset>
)====";

const char html_init_save[] PROGMEM =  R"====(
<p> Rebooting and connecting to your WiFi network! You should see it listed in on your access point. </p>
)====";

const char html_init_reboot[] PROGMEM =  R"====(
<p> Rebooting... </p>
)====";
