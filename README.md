# IceStreamer
A simple live audio streamer, intended as a replacement for DarkIce.

Supported capture interfaces:
* Jack
* ALSA
* Pulseaudio

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

    # Here you can set any properties of GStreamer's jackaudiosrc, alsasrc, pulsesrc
    # In this example, device is a property of alsasrc.
    # See 'gst-inspect-1.0 alsasrc' for documentation
    device=hw:0,0

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
