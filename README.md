# IceStreamer
A simple live audio streamer, intended as a replacement for DarkIce.

IceStreamer supports only a single input source, which it can encode to multiple
formats, in parallel, and send to multiple servers. It aims to cover the simple
usecase of streaming audio for a web radio station.

Robustness in IceStreamer is of major concern, so the most important feature
perhaps is that it never stops when there are network errors while sending.
If a certain stream is failing to send, it will reset its state and try
establishing a new connection after a while. Note, though, that this feature does
not work very well with GStreamer versions prior to 1.13.1, as it takes way too long
to timeout (see https://bugzilla.gnome.org/show_bug.cgi?id=571722 for details).

Supported capture interfaces:
* Jack
* ALSA
* PulseAudio
* PipeWire

Supported encoders:
* Ogg/Vorbis
* Ogg/Opus
* Webm/Vorbis
* Webm/Opus
* MP3

Supported streaming servers:
* IceCast (1.3.x and 2.x)
* ShoutCast

## Dependencies
IceStreamer is built using GStreamer. For compilation, you will need the core GStreamer
headers installed and at runtime you are also expected to have the following plugins
installed:

### From gstreamer-plugins-base:
* vorbisenc
* opusenc
* oggmux
* alsasrc
* audioconvert
* audioresample
* audiotestsrc

### From gstreamer-plugins-good:
* shout2send
* webmmux
* pulsesrc
* jackaudiosrc

### From gstreamer-plugins-ugly:
* lamemp3enc

## Configuration
By default IceStreamer reads configuration from a file called icestreamer.conf
in the current working directory. Alternatively, you may specifiy a different
configuration file with the -c/--config command line switch.

The configuration file should be in the following format:

    [input]
    # Supported sources: jack, alsa, pulse, test, auto (the default)
    source=alsa

    # Here you can set any properties of the source element:
    # jackaudiosrc, alsasrc, pulsesrc, pipewiresrc
    # In this example, device is a property of alsasrc.
    # See 'gst-inspect-1.0 alsasrc' for documentation
    device=hw:0,0

    # Additionally, input format, channels & rate can optionally be set here
    #format=S16LE
    #channels=2
    #rate=48000

    [stream1]
    # Supported encoders: opus, vorbis, mp3
    encoder=opus

    # Supported containers: ogg, webm
    # Note that this has no effect when encoder=mp3
    container=ogg

    # Here you can set any properties of GStreamer's shout2send element
    # See 'gst-inspect-1.0 shout2send' for documentation
    ip=rs.radio.uoc.gr
    port=8000
    password=<censored>
    mount=test.ogg
    streamname=Test
    description=Test stream
    genre=Misc
    url=http://rs.radio.uoc.gr:8000/test.ogg
    public=true

    # You can also include properties of vorbisenc, opusenc, lamemp3enc, oggmux, webmmux
    # In this example, bitrate is a property of opusenc, expressed in bps.
    # See 'gst-inspect-1.0 opusenc' for documentation
    bitrate=128000

    [stream2]
    encoder=mp3

    ip=rs.radio.uoc.gr
    port=8000
    password=<censored>
    mount=test.mp3
    streamname=Test
    description=Test stream
    genre=Misc
    url=http://rs.radio.uoc.gr:8000/test.mp3
    public=true

    # Properties of lamemp3enc
    # See 'gst-inspect-1.0 lamemp3enc' for documentation
    target=bitrate
    cbr=true
    bitrate=128
