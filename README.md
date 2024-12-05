# scuff (work in progress)
 
I am working on a cross-platform CLAP/VST3 audio plugin sandboxing system for Windows, macOS and Linux. Since this is not an easy problem to solve, I am trying to design the system to be generic enough that nobody has to figure it out ever again. They can just use this instead. The project consists of:
- [A sandbox executable](sbox) responsible for hosting one or more plugin instances.
- [A scanner executable](scan) which can scan the system for installed plugins.
- [A static library](client) which encapsulates the sandboxing system and allows us to use it within our audio applications.

Here is a blog post about this project: https://www.patreon.com/posts/plugin-110821252

Here is the interface of the client library in its current state: [client/include/scuff/client.hpp](client/include/scuff/client.hpp)

## Current status

### The scanner
Finished for CLAP plugins.

### The sandboxing backend and framework
Pretty much done. By this I'm talking about all the sandbox process management, shared memory management, interprocess communication, and the client API. Everything appears to work well on Windows. Have not yet done enough testing on macOS and Linux.

### CLAP-based audio effects
About 99% done. I worked towards this goal first because this is what I need for my own project. More tests need to be written.

### CLAP-based instruments or other kinds of devices
Not done but the framework is all there. I just don't need this for my own project yet so I'm not working on it.

### VST3 support
Not started at all because I don't need it for my own project but everything is sort of written with this in mind.

## Glossary

### Device
An instance of a CLAP or VST3 plugin. A device can be connected to other devices, even if they are in different sandbox processes, as long as they exist within the same **sandbox group**.

### Sandbox
A process consisting of one or more **devices**. The hairy problem of communicating with the sandbox processes is encapsulated by the client library. A sandbox is always part of a **sandbox group**.

### Sandbox Group
A collection of sandboxes which can be processed as a group. Audio and event data can flow between sandboxes within the same group.

## Basic concept

Here are some basic example scenarios and I will try to describe how they work in this system. The arrows represent audio signal flow and I am ignoring event data. Each of these illustrates a single sandbox group. Clients are free to create as many sandbox groups as they like.

### Scenario 1
![scuff01](https://github.com/user-attachments/assets/049b3659-bd3a-4e4f-9c97-8f42e7ebca41)

This is a simple effect rack consisting of three devices in series. Each device belongs to a different sandbox process. The DAW starts by writing the audio signal to the shared memory segment of the input audio port of the first device. The DAW then signals every sandbox in the sandbox group to begin processing. Each device reads whatever is currently written to their input ports. For device 1 that would be the audio signal that the DAW just wrote. For devices 2 and 3 it's going to be whatever was written there on the previous iteration of the audio processing, so there is a buffer of latency introduced in between each sandbox. The DAW waits for all sandboxes to finish processing and then copies the outputs of each device to the connected inputs.

### Scenario 2
![scuff02](https://github.com/user-attachments/assets/69a485d3-82d5-4762-9e86-f9d957715e92)

This is the same effect rack except every device belongs to one big sandbox. If one plugin crashes then it brings the entire sandbox process down with it. The sandbox still logically exists, it's just in a crashed state. The client library has a function to restart the sandbox which will attempt to re-launch the sandbox process and recreate all the devices within it. There is an invisible auto-save system which periodically saves dirty device states to be used when restoring them after a crash.

### Scenario 3
![scuff04](https://github.com/user-attachments/assets/97d7a7b7-fcc0-4fde-a1b0-a1f7088b2e96)

Here devices 1 and 2 belong to the same sandbox process. An audio connection between two devices inside the same sandbox is treated differently than a connection which goes between two different sandboxes. When the device graph changes we calculate the correct order of processing for each device in each sandbox so when the red sandbox is processed it will process the entire signal chain (device 1, then device 2), so only one buffer of latency is introduced in this chain.

### Scenario 4
![scuff03](https://github.com/user-attachments/assets/33a527dc-3d04-4ca9-838c-0b751b54c935)

This is fine! Each sandbox process is simply reading whatever data is waiting at the input ports at the time of processing. For device 1 it's the data that was just written by the DAW. For the other two devices there will be latency introduced into the signal chain.

### Scenario 5
![scuff05](https://github.com/user-attachments/assets/9a9c716f-bea6-4023-95c0-97ce088747af)

Here the DAW is reading device 1's output signal. The DAW can do this for any device (perhaps is wants to animate a signal level meter, or split the signal and send it somewhere else.) The data is all there in shared memory and the DAW is free to read it. The DAW is also routing a second input into device 3 (maybe it's something like a vocoder or sidechain compressor which takes in a second audio input). This just works the same as device 1's input at the start of the chain - the audio signal is written to that audio port in shared memory.

### Scenario 6
![scuff06](https://github.com/user-attachments/assets/5cc893fe-95e2-485b-b3f6-49d9bbf51adc)

A feedback loop like this within a single sandbox is also fine. When the feedback loop is detected the output of the feedbacking device will simply be written to the target input buffer, instead of continuing the process chain immediately. If the feedback loop occurs across multiple sandboxes then it is resolved automatically by the system already described, without us having to do anything more.
