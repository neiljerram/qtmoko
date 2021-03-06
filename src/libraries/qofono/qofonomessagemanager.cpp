/*
 * This file was generated by qofonodbusxml2cpp version 0.7
 * Command line was: qofonodbusxml2cpp -p qofonomessagemanager ofono.xml org.ofono.MessageManager
 *
 * qofonodbusxml2cpp is Copyright (C) 2009 Nokia Corporation and/or its subsidiary(-ies).
 *
 * This is an auto-generated file.
 * This file may have been hand-edited. Look for HAND-EDIT comments
 * before re-generating it.
 */

#include "qofonomessagemanager.h"

/*
 * Implementation of interface class OrgOfonoMessageManagerInterface
 */

OrgOfonoMessageManagerInterface::OrgOfonoMessageManagerInterface(const QString &service, const QString &path, const QDBusConnection &connection, QObject *parent)
    : QOFonoDbusAbstractInterface(service, path, staticInterfaceName(), connection, parent)
{
    qRegisterMetaType<QOFonoObject>("QOFonoObject");
    qDBusRegisterMetaType<QOFonoObject>();
    qRegisterMetaType<QOFonoObjectList>("QOFonoObjectList");
    qDBusRegisterMetaType<QOFonoObjectList>();
}

OrgOfonoMessageManagerInterface::~OrgOfonoMessageManagerInterface()
{
}

