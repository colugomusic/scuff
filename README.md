# scuff (work in progress)
 
I am working on a cross-platform CLAP/VST audio plugin sandboxing system. Since this is not an easy problem to solve, I am trying to design the system to be generic enough that nobody has to figure it out ever again. They can just use this instead. The project consists of:
- [A sandbox executable](sbox) responsible for hosting one or more plugin instances.
- [A scanner executable](scan) which can scan the system for installed plugins.
- [A static library](client) which encapsulates the sandboxing system and allows us to use it within our audio applications.
- [A test host](test-host) for testing.

If you have expertise in this area and want to help then please get in touch.

I am targeting Windows, macOS and Linux. My immediate plan is to start by implementing CLAP support only and then add VST support later. I am planning to use [nappgui](https://github.com/frang75/nappgui_src) to handle opening editor windows within the sandbox processes.

Here is a blog post about this project: https://www.patreon.com/posts/plugin-110821252

Here is the interface of the client library in its current state: [client/include/scuff/client.h](client/include/scuff/client.h)

## Glossary

### Device
An instance of a CLAP or VST plugin. A device can be connected to other devices, even if they are in different sandbox processes, as long as they exist within the same **sandbox group**.

### Sandbox
A process consisting of one or more **devices**. The hairy problem of communicating with the sandbox processes is encapsulated by the client library. A sandbox is always part of a **sandbox group**.

### Sandbox Group
A collection of sandboxes which can be processed as a group. Audio and event data can flow between sandboxes within the same group.

## Basic concept

Here are some basic example scenarios and I will try to describe how they will work in this system. The arrows represent audio signal flow and I am ignoring event data.

### Scenario 1
![scuff01](https://github.com/user-attachments/assets/049b3659-bd3a-4e4f-9c97-8f42e7ebca41)

This is a simple effect rack consisting of three devices in series. Each device belongs to a different sandbox process. The DAW starts by writing the audio signal to the shared memory segment of the input audio port of the first device. There's a double-buffering system here where the audio signal is written to a back buffer and read from a front buffer. The DAW then signals every sandbox in the sandbox group to begin processing. Each device reads whatever is in the front buffer of their audio input ports. For device 1 that would be the audio signal that the DAW just wrote. For devices 2 and 3 it's going to be whatever was written to the back buffer in the previous iteration of the sandbox group processing. After signalling the sandboxes to start processing, the DAW reads the signal in the front buffer of the output port of device 3 (i.e. it reads device 3's result from the previous iteration.) So in the end there is one buffer of latency introduced for each sandbox in the group. Before reading the output, the DAW does have to make sure that the output of the previous iteration of the sandbox group is actually ready. If it isn't then it will have to busy-wait at that point.

### Scenario 2
![scuff02](https://github.com/user-attachments/assets/69a485d3-82d5-4762-9e86-f9d957715e92)

This is the same effect rack except every device belongs to one big sandbox. There's only one buffer of latency introduced for the entire sandbox group, but if one plugin crashes then it brings the entire sandbox process down with it. The sandbox still logically exists, it's just in a crashed state. The client library has a function to restart the sandbox which will attempt to re-launch the sandbox process and recreate all the devices within it. This might benefit from some kind of auto-save system which periodically saves device state to be used when restoring them after a crash (haven't thought about that properly yet.)

### Scenario 3
![scuff04](https://github.com/user-attachments/assets/97d7a7b7-fcc0-4fde-a1b0-a1f7088b2e96)

Here devices 1 and 2 belong to the same sandbox process. An audio connection between two devices inside the same sandbox is treated differently than a connection which goes between two different sandboxes. When the device graph changes we calculated the correct order of processing for each device in each sandbox so when the red sandbox is processed it will process the entire signal chain (device 1, then device 2), so only one buffer of latency is introduced for the first 2 devices. Again, the output of device 2 is being written to the back buffer of its output port (in shared memory), and device 3 in the green sandbox is reading from the front buffer, so that's where the latency comes from when audio flows from one sandbox to another.

### Scenario 4
![scuff03](https://github.com/user-attachments/assets/33a527dc-3d04-4ca9-838c-0b751b54c935)

This is fine! Each sandbox process is simply reading from the front buffers of the connected input ports. In the red sandbox, both devices 1 and 3 are reading from the same back buffer of audio input ports, with 1 buffer of latency.

### Scenario 5
![scuff05](https://github.com/user-attachments/assets/9a9c716f-bea6-4023-95c0-97ce088747af)

Here the DAW is reading device 1's output signal from the front buffer. The DAW can do this for any device (perhaps is wants to animate a signal level meter, or split the signal and send it somewhere else.) The data is all there in shared memory and the DAW is free to read it. The DAW is also routing a second input into device 3 (maybe it's something like a vocoder or sidechain compressor which takes in a second audio input). This just works the same as device 1's input at the start of the chain - the audio signal is written to the back buffer of that audio port in shared memory.
