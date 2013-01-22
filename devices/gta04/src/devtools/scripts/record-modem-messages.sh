#!/bin/sh

. /opt/qtmoko/qpe.env
daemon -iU -o /modem_messages.log -- gta04-modem-messages -l 2 -k 200
sleep 5

