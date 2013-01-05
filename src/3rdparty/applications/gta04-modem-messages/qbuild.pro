TEMPLATE=app
TARGET=gta04-modem-messages

CONFIG+=qtopia
DEFINES+=QTOPIA

# I18n info
STRING_LANGUAGE=en_US
LANGUAGES=en_US

# Package info
pkg [
    name=gta04-modem-messages
    desc="Extract debugging message from the GTA04 modem"
    license=LGPL
    version=1.0
    maintainer="Neil Jerram <neil@ossau.homelinux.net>"
]

SOURCES=gta04-modem-messages.c

# Install rules
target [
    hint=sxe
    domain=untrusted
]
