# XIAOGPT
This is a miniaturized version of my <A href=https://github.com/astromikemerri/ESPGPT>ESPGPT project</a>, designed to see quite how small a box I could fit it in.  I also added in voice cloning using the ElevenLabs API, and a RAG layer with biographical facts to inform the ChatGPT responses, to create a digital mini-me, or mini-you if you would like to build your own!

To make things as tiny as possible, I have switched to an even smaller microcontroller (with the added benefit of built-in battery management for a stand-alone device), removed the push-button-to-speak by using simple voice activation instead, done away with the SD card for storing audio files by using a SPIFFS file system for storage in microcontroller memory, used a smaller speaker, and replaced the separate LED status indicator with the on-board LED.

So, the hardware required for this stripped-down version is just:
<ul>
  <li> Seeed Studio XIAO ESP32S3 microcontroller</li>
  <li> INMP441 I2S microphone</li>
  <li> Max98357 I2S audio amplifier breakout board</li>
  <li> 3W 8 ohm speaker</li>
  <li> (and a suitable rechargeable battery like a 3.7V 300mAh 602030 Li-Po and an on-off switch if you want to create a stand-alone device) </li>
</ul>
This should all <b>just</b> <A href=XIAOGPTfritzing.jpg>fit on a mini breadboard</A> (here's <A href=XIAOGPT.fzz>the Fritzing file</A>), and end up <A href=XIAOGPTimage.jpeg>looking something like this</A> (with a 10p for scale):
<p></p>
<p>
<img src=XIAOGPTbreadboard.jpg>
</p>
<A HREF=XIAOGPTdist.ino>The code to make all of this work</a> calls APIs from both OpenAI (for speech-to-text and ChatGPT) and ElevenLabs (with whom I generated a clone of my voice for the text-to-speech implementation, so I could get the gizmo to pretend to be me).  If you want to keep things simpler and don't want to use a voice clone, check out the code for the <A href=https://github.com/astromikemerri/ESPGPT>original project</a>, which just uses the generic OpenAI TTS, so no need to sign up with ElevenLabs. Note that to use these APIs, you need access to both OpenAI and ElevenLabs, <b>WHICH MAY COST MONEY</b>.  Information on the cost of these services is available for both <A href=https://openai.com/api/pricing/>OpenAI</a> and <a href=https://elevenlabs.io/pricing>ElevenLabs</a>.
<p></p>
Since my ambition here was to try and make this gizmo pretend to be me, I also added in a simple RAG (retrieval-augmented generation) layer to the call to ChatGPT, which searches a biographical database for keywords in each prompt, and prepends any biographical information retrieved to the ChatGPT call.  The database is a text file called "bio.txt" that you upload to the SPIFFS file system before running the main code.  The file format is just a series of lines comprising the keywords you want to check for and the associated biographical facts, separated by a colon, so you can populate the file with as much biographical information as you require like this:

```
birthplace, born: you were born in Milton Keynes in 1875.
food: you eat pretty much anything except porridge.  You particularly like oranges.
job, work, employment: You worked for your entire career as an accountant in a large firm.  You are now retired.
current project, experiment, ESP32, AI, ChatGPT: You are working on a voice-activated ChatGPT interface using an ESP32.
hobbies, pastimes, fantasies, fantasy: you enjoy stamp collecting, and making up entirely fictional autobiographical facts.
```

Depending on which IDE you are using, there may be no straightforward way to upload this file to SPIFFS.  <A href=UploadBio.ino>Here</a> is a sketch that exploits the ESP32's ability to host a web server, as a simple way to do so.  Upload this sketch to the ESP32, connect your PC's WiFi to ESP32_AP (password 12345678), open a web browser to 192.168.4.1, select and upload the file called "bio.txt", and then you can install <A HREF=XIAOGPTdist.ino>the main XIAOGPT sketch</A>.

Once all this is done, the gizmo should work <A HREF=XIAOGPT.mov>something like this</a>. 

<img src=XIAOGPTsolder.jpg align=right width=33%>
And, just to finish the project off, I soldered together the components rather than using a breadboard, attached a battery and on/off switch to the pads on the back of the XIAO ESP32S3, and squeezed the whole thing into a tiny box.  You can listen in on a slightly longer chat that I had with this stand-alone boxed version of myself <A HREF=https://youtu.be/_BVf_5BmFiE>here on YouTube</A>.
<BR><P></P>
<img src=XIAOGPTbox.jpg align=left width=33%>
It is worth noting that, in addition to providing the engine that makes this gizmo work, ChatGPT was also responsible for coming up with many of the ideas (like using a RAG layer to provide biographical details, and how to upload the biographical database file to SPIFFS), producing most of the code (which is why it is functional rather than beautiful!), and even for generating <A href=XIAOGPT.stl>the CAD  model</a> from which I 3D printed the box to put the final version inside.

<p></p>

It is an indication of what someone with even my limited technical skills can achieve with the assistance of an LLM!
