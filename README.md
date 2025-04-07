# XIAOGPT
This is a miniaturized version of my <A href=https://github.com/astromikemerri/ESPGPT>ESPGPT project</a>, designed to see quite how small a box I could fit it in.  I also added in voice cloning using the ElevenLabs API, and a RAG layer with biographical facts about me to inform the ChatGPT responses, to create a digital mini-me!

To make things as small as possible, I have switched to an even smaller microcontroller (with the added benefit of built-in battery management for a stand-alone device), removed the push-button-to-speak by using simple voice activation instead, done away with the SD card for storing audio files by using a SPIFFS file system for storage in microcontroller memory, and replaced the separate LED status indicator with the on-board LED.

So, the hardware required for this stripped-down version is just:
<ul>
  <li> Seeed Studio XIAO ESP32S3 microcontroller</li>
  <li> INMP441 I2S microphone</li>
  <li> Max98357 I2S audio amplifier breakout board</li>
  <li> 3W 8 ohm speaker</li>
  <li> (and a suitable recahrgeable battery like a 3.7V 300mAh 602030 Li-Po and an on-off sswitch if you want to create a stand-alone version) </li>
</ul>
This should all fit on a mini breadboard (<A href=XIAOGPT.fzz>here's the Frizing file</A>):

<img src=ESPGPTfritzing.jpg width=500>

and physically -end up looking something like this:

<img src="XIAOGPTimage.jpg" width=500>


To get this to work, you need to have an <A href= https://platform.openai.com/account/>OpenAI account</a>, to set up a form of payment in the <A href=https://platform.openai.com/settings/organization/billing/overview> "Billing" section</a> and prepay for some API credits (NB. NOT FREE! But it shouldn't cost much to run this small project -- all the playing around in development has only cost me around $10. And you can set limits in the <A href=https://platform.openai.com/settings/organization/limits>"Limits" section</a> to ensure that you do not spend more than you want).  You can then create an API key <A href=https://platform.openai.com/api-keys>here</a>, which you will need to paste into <A href=ESPGPTcode.ino>the code</a>, along with your WIFI credentials.

The libraries in the code are all fairly standard and easy to find and install; the audio library I used is https://github.com/earlephilhower/ESP8266Audio.

If all goes to plan, you should end up being able to hold a conversation <A href=ESPGPT.mov>like this</a>. You can also see the dialogue on the serial monitor, if the ESP32 is connected to your computer.

You can ring the changes on which spoken voice you use, which ChatGPT model, its "temperature" (ie, how random its answers are) and the NCONV parameter, which specifies how long a conversation the code remembers.

Also, feel free to play around with your own version of the code.  For example, <A href="ESPGPTvoice.ino">here</a> is a draft that I am currently tinkering with where the conversation is voice-activated, triggered by the volume level that the microphone picks up.  Presses of the button just pause and resume the conversation. If you find the sound triggering isn't working well, try pausing and resuming the conversation, as this also makes the code recalibrate the background noise level.

Have fun!
