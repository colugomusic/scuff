# scuff
 
I am working on a cross-platform CLAP/VST audio plugin sandboxing system, designed to be generic enough that nobody has to figure it out ever again. The project consists of:
- A sandbox executable responsible for hosting one or more plugin instances.
- A scanner executable which can scan the system for installed plugins.
- A static library which encapsulates the sandboxing system and allows us to use it within our audio applications.

I've never done anything like this before so my ideas are all theoretical at the moment. If you have expertise in this area and want to help then please get in touch.

I am targeting Windows, macOS and Linux. My immediate plan is to start by implementing CLAP support only and then add VST support later. I am planning to use [nsappgui](https://github.com/frang75/nappgui_src) to handle opening editor windows within the sandbox processes.

Here is a blog post about this project: https://www.patreon.com/posts/plugin-110821252

Here is the interface of the client library in its current state: [client/include/scuff/client.h](client/include/scuff/client.h)

## Glossary

### Device
An instance of a CLAP or VST plugin. A device can be connected to other devices, even if they are in different processes, as long as they exist within the same **sandbox group**.

### Sandbox
A process consisting of one or more **devices**. The hairy problem of communicating with the sandbox processes is encapsulated by the client library. A sandbox always part of a **sandbox group**.

### Sandbox Group
A collection of sandboxes which can be processed as a group. Audio and event data can flow between sandboxes within the same group.
