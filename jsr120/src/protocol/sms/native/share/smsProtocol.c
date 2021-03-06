/*
 *
 *
 * Copyright  1990-2007 Sun Microsystems, Inc. All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 only, as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License version 2 for more details (a copy is
 * included at /legal/license.txt).
 * 
 * You should have received a copy of the GNU General Public License
 * version 2 along with this work; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 * 
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 or visit www.sun.com if you need additional
 * information or have any questions.
 */

/*
 * SUBSYSTEM: WMA (Wireless Messaging API)
 * FILE:      smsProtocol.c
 * OVERVIEW:  This file handles WMA functions such as
 *            - opening an SMS connection
 *            - sending an SMS message to a destination address
 *            - receiving an SMS message
 *            - closing the connection
 */

#include <stdio.h>
#include <string.h>
#include <kni.h>
#include <kni_globals.h>
#include <sni.h>
#include <sni_event.h>

#include <jsr120_sms_protocol.h>
#include <jsr120_sms_pool.h>
#include <jsr120_sms_listeners.h>
#if ENABLE_PCSL
#include <java_types.h>
#include <pcsl_memory.h>
#include <pcsl_string.h>
#endif


/*
 * Protocol methods
 */
unsigned char smsBuffer[sizeof(SmsMessage) + MAX_MSG_BUFFERSIZE - 1];

typedef struct {
    unsigned char *pAddress;
    unsigned char *pMessageBuffer;
    void *pdContext;
} jsr120_sms_message_state_data;


/**
 * Opens an SMS connection.
 *
 * @param host The host name.
 * @param port The port number used to match against incoming SMS messages.
 *
 * @return A handle to the open SMS connection.
 */
KNIEXPORT KNI_RETURNTYPE_INT
Java_com_sun_cldc_io_j2me_sms_Protocol_open0(void) {

    int handle = 0;
#if ENABLE_PCSL

    int port;
    /** The MIDP String version of the host. */
    pcsl_string psHost;
    /* The host name for this connection. */
    unsigned char* host = NULL;
    /* The midlet suite name for this connection. */
    SuiteIdType msid = UNUSED_SUITE_ID;

    port = KNI_GetParameterAsInt(3);

    /* When port is 65535>=port>=0 then return else continue */
    if ((port>=0) && (port<65536)) {
        /* Create handles for all Java objects. */
        KNI_StartHandles(1);
        KNI_DeclareHandle(javaStringHost);

        /* Pick up the host string. */
        KNI_GetParameterAsObject(1, javaStringHost);
        /* Pick up the Midlet Suite ID. */
        msid = KNI_GetParameterAsInt(2);

        do {
            /*
             * Get unique handle, to identify this
             * SMS "session"..
             */
            handle = (int)(pcsl_mem_malloc(1));
            if (handle == 0) {
                KNI_ThrowNew(KNIOutOfMemoryError,
                            "Unable to start SMS.");
                break;
            }

            /* Pick up the host name, if any. */
            if (!KNI_IsNullHandle(javaStringHost)) {

                psHost.length = KNI_GetStringLength(javaStringHost)+1;
                psHost.data = (jchar*)pcsl_mem_malloc(psHost.length * sizeof(jchar));
				memset(psHost.data, 0, psHost.length * sizeof(jchar));
				psHost.flags = 0;
                if (psHost.data == NULL) {
                    /* Couldn't allocate space for the host name string. */
                    KNI_ThrowNew(KNIOutOfMemoryError, NULL);
                    break;
                } else {

                    /* Convert the MIDP string contents to a character array. */
                    KNI_GetStringRegion(javaStringHost, 0, psHost.length-1, psHost.data);
                    host = (unsigned char*)pcsl_string_get_utf8_data(&psHost);
                    pcsl_mem_free(psHost.data);
                }
            }

            /*
             * Register the port with the message pool, only if this
             * is a server connection (NULL host name.).
             */
            if (host == NULL) {

                /* register SMS port with SMS pool */
                if (jsr120_is_sms_midlet_listener_registered((jchar)port) == WMA_ERR) {

                    if (jsr120_register_sms_midlet_listener((jchar)port,
                                                        msid,
                                                        handle) == WMA_ERR) {

                        KNI_ThrowNew(KNIIOException, "WMA listener register failed");
                        break;
                    }
                } else {
                    /* port already registered, throw exception */
                    KNI_ThrowNew(KNIIOException, "already in use");
                    break;
                }
            }
        } while (0);

        /* Memory clean-up (The strings can be NULL). */
        pcsl_mem_free(host);

        KNI_EndHandles();
    }
#endif
    KNI_ReturnInt(handle);
}

#if ENABLE_PCSL

/**
 * Internal helper function implementing connection close routine
 *
 * @param port The port associated with this connection.
 * @param handle The handle of the open SMS message connection.
 * @param deRegister Deregistration os the port when parameter is 1.
 * @param unblockRead if true, unblock any reading blocked threads
 */
static void closeConnection(int port, int handle, int deRegister, jboolean unblockRead) {

    if (port >= 0 && handle != 0) {

#ifdef DELETE_WMA_MSGS
        SmsMessage* lostMessage;
        while (lostMessage = jsr120_sms_pool_retrieve_next_msg(port)) {
            jsr120_sms_delete_msg(lostMessage);
        }
#endif

		if (KNI_TRUE == unblockRead) {
        	/** unblock any blocked threads */
        	jsr120_sms_unblock_thread((jint)handle, WMA_SMS_READ_SIGNAL);
		}

        if (deRegister) {
            /** unregister SMS port from SMS pool */
            jsr120_unregister_sms_midlet_listener((jchar)port);

            /* Release the handle associated with this connection. */
            pcsl_mem_free((void *)handle);
        }

    }
}
#endif

/**
 * Closes an open SMS connection.
 *
 * @param port The port associated with this connection.
 * @param handle The handle of the open SMS message connection.
 * @param deRegister Deregistration port when parameter is 1.
 *
 * @return <code>0</code> if successful; <code>-1</code> if failed.
 */
KNIEXPORT KNI_RETURNTYPE_INT
Java_com_sun_cldc_io_j2me_sms_Protocol_close0(void) {
    int status = 0;
#if ENABLE_PCSL
    int port = 0;
    int handle = 0;
	jboolean flag = 0;

    /** Deregistration flag. */
    int deRegister;

    port = KNI_GetParameterAsInt(1);
    handle = KNI_GetParameterAsInt(2);
    deRegister = KNI_GetParameterAsInt(3);
	flag = KNI_GetParameterAsBoolean(4);

    closeConnection(port, handle, deRegister, flag);
#endif
    KNI_ReturnInt(status);
}

/**
 * Sends an SMS message.
 *
 * @param handle The handle to the open SMS connection.
 * @param messageType The type of message: binary or text.
 * @param address The SMS-formatted address.
 * @param destPort The port number of the recipient.
 * @param sourcePort The port number of the sender.
 * @param messageBuffer The buffer containing the SMS message.
 *
 * @return Always returns <code>0</code>.
 */
KNIEXPORT KNI_RETURNTYPE_INT
Java_com_sun_cldc_io_j2me_sms_Protocol_send0(void) {
#if ENABLE_PCSL
    WMA_STATUS status = WMA_ERR;
    jint messageLength = 0;
    jint messageType;
    jint sourcePort;
    jint destPort;
    jint handle;
    pcsl_string psAddress;
    unsigned char *pAddress = NULL;
    unsigned char *pMessageBuffer = NULL;
    SNIReentryData *info;
    jboolean stillWaiting = KNI_FALSE;
    jboolean trySend = KNI_FALSE;
    void *pdContext = NULL;
    jsr120_sms_message_state_data *messageStateData = NULL;
    jint bytesSent;
    jboolean isOpen;

    KNI_StartHandles(4);

    KNI_DeclareHandle(thisPointer);
    KNI_DeclareHandle(thisClass);
    KNI_GetThisPointer(thisPointer);
    KNI_GetObjectClass(thisPointer, thisClass);
    isOpen = KNI_GetBooleanField(thisPointer, KNI_GetFieldID(thisClass, "open", "Z"));
    
    if (isOpen) { /* No close in progress */
        KNI_DeclareHandle(messageBuffer);
        KNI_DeclareHandle(address);

        handle = KNI_GetParameterAsInt(1);
        messageType = KNI_GetParameterAsInt(2);
        KNI_GetParameterAsObject(3, address);
        destPort = KNI_GetParameterAsInt(4);
        sourcePort = KNI_GetParameterAsInt(5);
        KNI_GetParameterAsObject(6, messageBuffer);

        do {
            info = (SNIReentryData*)SNI_GetReentryData(NULL);
            if (info == NULL) {	  /* First invocation. */

                if (KNI_IsNullHandle(address)) {

                    KNI_ThrowNew(KNIIllegalArgumentException, NULL);
                    break;
                } else {
                    psAddress.length= KNI_GetStringLength(address)+1;
                    psAddress.data = (jchar *)pcsl_mem_malloc(psAddress.length * sizeof (jchar));
					psAddress.flags = 0;
					memset(psAddress.data, 0, psAddress.length * sizeof(jchar));
                    if (psAddress.data == NULL) {

                        KNI_ThrowNew(KNIOutOfMemoryError, NULL);
                        break;
                    } else {

                        KNI_GetStringRegion(address, 0, psAddress.length-1, psAddress.data);
                        pAddress = (unsigned char*)pcsl_string_get_utf8_data(&psAddress);
						if (!pAddress) {
							KNI_ThrowNew(KNIOutOfMemoryError, NULL);
                        	break;
						}
                        pcsl_mem_free(psAddress.data);

                        if (!KNI_IsNullHandle(messageBuffer)) {
                            messageLength = KNI_GetArrayLength(messageBuffer);
                        }
                        if (messageLength >= 0) {

							if (messageLength > 0) {
                                pMessageBuffer = (unsigned char *)pcsl_mem_malloc(messageLength);
								if (!pMessageBuffer) {
									KNI_ThrowNew(KNIOutOfMemoryError, NULL);
									break;
								} else {
                                	memset(pMessageBuffer, 0, messageLength);
                                	KNI_GetRawArrayRegion(messageBuffer, 0, messageLength,
                                                      (jbyte *)pMessageBuffer);
								}
                            }
							
							trySend = KNI_TRUE;
							
                        }
                    }
                }
            } else { /* Reinvocation after unblocking the thread. */
                messageStateData = (jsr120_sms_message_state_data*)info->pContext;
                pMessageBuffer = messageStateData->pMessageBuffer;
                pAddress = messageStateData->pAddress;
                pdContext = messageStateData->pdContext;

                trySend = KNI_TRUE;
            }

            if (trySend) {
                /* send message. */
                status = jsr120_send_sms((jchar)messageType,
                                         pAddress,
                                         pMessageBuffer,
                                         (jchar)messageLength,
                                         (jchar)sourcePort,
                                         (jchar)destPort,
                                         &bytesSent,
                                         &pdContext);

                if (status == WMA_ERR) {
                    KNI_ThrowNew(KNIIOException, "Sending SMS");
                    break;
                } else if (status == WMA_NET_WOULDBLOCK) {
                    if (messageStateData == NULL) {
                        messageStateData =
                            (jsr120_sms_message_state_data *)pcsl_mem_malloc(
                                sizeof(*messageStateData));
                        messageStateData->pMessageBuffer = pMessageBuffer;
                        messageStateData->pAddress = pAddress;
                    }

                    messageStateData->pdContext = pdContext;

                    /* Block calling Java Thread. */
                    SNIEVT_wait(WMA_SMS_WRITE_SIGNAL, handle,
                                     messageStateData);

                    stillWaiting = KNI_TRUE;
                    break;
                } else {
                    /*
                     * Message successfully sent.
                     * Call send completion function.
                     */
                    //jsr120_notify_sms_send_completed(&bytesSent);
                }
            }
        } while (0);

        if (!stillWaiting) {
            if (pMessageBuffer) pcsl_mem_free(pMessageBuffer);
            if (pAddress) pcsl_mem_free(pAddress);
			if (messageStateData) pcsl_mem_free(messageStateData);
        }
    }

    KNI_EndHandles();
#endif
    KNI_ReturnInt(0); /* currently ignored. */
}

/**
 * Receives an SMS message.
 *
 * @param port The port number to be matched against incoming SMS messages.
 * @param handle The handle to the open SMS message connection.
 * @param messageObject The Java message object to be populated.
 *
 * @return The length of the SMS message (in bytes).
 */
KNIEXPORT KNI_RETURNTYPE_INT
Java_com_sun_cldc_io_j2me_sms_Protocol_receive0(void) {
    int messageLength = -1;

#if ENABLE_PCSL
    SNIReentryData *info = (SNIReentryData*)SNI_GetReentryData(NULL);
    int port, handle;
    SmsMessage *psmsData = NULL;
    /* The midlet suite name for this connection. */
    jboolean isOpen;

    KNI_StartHandles(6);

    KNI_DeclareHandle(thisPointer);
    KNI_DeclareHandle(thisClass);
    KNI_GetThisPointer(thisPointer);
    KNI_GetObjectClass(thisPointer, thisClass);
    isOpen = KNI_GetBooleanField(thisPointer, KNI_GetFieldID(thisClass, "open", "Z"));

    if (isOpen) { /* No close in progress */
        KNI_DeclareHandle(messageClazz);
        KNI_DeclareHandle(messageObject);
        KNI_DeclareHandle(addressArray);
        KNI_DeclareHandle(byteArray);

        port = KNI_GetParameterAsInt(1);
        //msid = KNI_GetParameterAsInt(2);
        handle = KNI_GetParameterAsInt(3);
        KNI_GetParameterAsObject(4, messageObject);

        do {
            if (!info) {
                psmsData = jsr120_sms_pool_peek_next_msg((jchar)port);
                if (psmsData == NULL) {
                    /* block and wait for a message. */
                    SNIEVT_wait(WMA_SMS_READ_SIGNAL, handle, NULL);
                }
            } else {
                /* reentry. */
                psmsData = jsr120_sms_pool_peek_next_msg((jchar)port);
            }

            if (psmsData != NULL) {
                if (NULL != (psmsData = jsr120_sms_pool_retrieve_next_msg(port))) {
                    /*
                     * A message has been retreived successfully. Notify
                     * the platform.
                     */
                    jsr120_notify_incoming_sms(psmsData->encodingType, psmsData->msgAddr,
                                               (unsigned char *)psmsData->msgBuffer,
                                               (jint)psmsData->msgLen,
                                               psmsData->sourcePortNum, psmsData->destPortNum,
                                               psmsData->timeStamp);

                    KNI_GetObjectClass(messageObject, messageClazz);
                    if(KNI_IsNullHandle(messageClazz)) {
                        KNI_ThrowNew(KNIOutOfMemoryError, NULL);
                        break;
                    } else {
                        jfieldID message_field_id = KNI_GetFieldID(messageClazz, "message","[B");
                        jfieldID address_field_id = KNI_GetFieldID(messageClazz, "address","[B");
                        jfieldID port_field_id = KNI_GetFieldID(messageClazz, "port","I");
                        jfieldID sentAt_field_id = KNI_GetFieldID(messageClazz, "sentAt","J");
                        jfieldID messageType_field_id = KNI_GetFieldID(messageClazz, "messageType","I");
                        if ((message_field_id == 0) || (address_field_id == 0) ||
                            (port_field_id == 0) || (sentAt_field_id == 0) ||
                            (messageType_field_id == 0)) {

                            /* REPORT_ERROR(LC_WMA, "ERROR can't get class field ID"); */

                            KNI_ThrowNew(KNIRuntimeException, NULL);
                            break;
                        } else {
                            messageLength = psmsData->msgLen;
                            if (messageLength >= 0) {
                                int i = 0;
                                int addressLength = strlen((char *)psmsData->msgAddr);

                                if (addressLength > MAX_ADDR_LEN){
                                    addressLength = MAX_ADDR_LEN;
                                }

                                if (messageLength > 0) {
                                    SNI_NewArray(SNI_BYTE_ARRAY, messageLength, byteArray);
                                    if (KNI_IsNullHandle(byteArray)) {

                                        KNI_ThrowNew(KNIOutOfMemoryError, NULL);
                                        break;
                                    } else {
                                        for (i = 0; i < messageLength; i++) {
                                            KNI_SetByteArrayElement(byteArray, i,
                                                                    psmsData->msgBuffer[i]);
                                        }

                                        KNI_SetObjectField(messageObject, message_field_id,
                                                           byteArray);
                                    }
                                }

                                if (addressLength >= 0) {
                                    SNI_NewArray(SNI_BYTE_ARRAY, addressLength, addressArray);
                                    if (KNI_IsNullHandle(addressArray)) {

                                        KNI_ThrowNew(KNIOutOfMemoryError, NULL);
                                        break;
                                    } else {
                                        for (i = 0; i < addressLength; i++) {
                                            KNI_SetByteArrayElement(addressArray, i,
                                                                 psmsData->msgAddr[i]);
                                        }

                                        KNI_SetObjectField(messageObject, address_field_id,
                                                           addressArray);
                                    }
                                }

                                KNI_SetIntField(messageObject, port_field_id,
                                                psmsData->sourcePortNum);
                                KNI_SetLongField(messageObject, sentAt_field_id,
                                                 psmsData->timeStamp);
                                KNI_SetIntField(messageObject, messageType_field_id,
                                                psmsData->encodingType);
                           }
                       }
                   }
               }
            }
        } while (0);

        jsr120_sms_delete_msg(psmsData);
    }

    KNI_EndHandles();
#endif
    KNI_ReturnInt(messageLength);
}

/**
 * Waits for a message to become available.
 *
 * @param port The port number to be matched against incoming SMS messages.
 * @param handle The handle to the open SMS message connection.
 *
 * @return the length of the unformatted message to be received.
 */
KNIEXPORT KNI_RETURNTYPE_INT
Java_com_sun_cldc_io_j2me_sms_Protocol_waitUntilMessageAvailable0(void) {
    int messageLength = -1;
#if ENABLE_PCSL
    SNIReentryData *info = (SNIReentryData*)SNI_GetReentryData(NULL);
    int port;
    int handle;

    SmsMessage *pSMSData = NULL;
    jboolean isOpen;

    KNI_StartHandles(2);

    KNI_DeclareHandle(thisPointer);
    KNI_DeclareHandle(thisClass);
    KNI_GetThisPointer(thisPointer);
    KNI_GetObjectClass(thisPointer, thisClass);
    isOpen = KNI_GetBooleanField(thisPointer, KNI_GetFieldID(thisClass, "open", "Z"));

    if (isOpen) { /* No close in progress */
        port = KNI_GetParameterAsInt(1);
        handle = KNI_GetParameterAsInt(2);

        if (port == -1) {
            /*
             * A receive thread wouldn't have been started with a port
             * of -1. But a connection can be closed and port set to -1,
             * just before this method is called.
             * The receiverThread uses the IllegalArgumentException to
             * check for this situation and gracefully terminates.
             */
            KNI_ThrowNew(KNIIllegalArgumentException, "No port available.");

        } else {

            /* See if there is a new message waiting in the pool. */
            pSMSData = jsr120_sms_pool_peek_next_msg1((jchar)port, 1);
            if (pSMSData != NULL) {
                messageLength = pSMSData->msgLen;
            } else {
                if (!info) {

                     /* Block and wait for a message. */
                    SNIEVT_wait(WMA_SMS_READ_SIGNAL, handle, NULL);

                } else {

                     /* May have been awakened due to interrupt. */
                     messageLength = -1;
                }
            }
        }
    }

    KNI_EndHandles();
#endif
    KNI_ReturnInt(messageLength);
}

/**
 * Computes the number of transport-layer segments that would be required to
 * send the given message.
 *
 * @param msgBuffer The message to be sent.
 * @param msgLen The length of the message.
 * @param msgType The message type: binary or text.
 * @param hasPort Indicates if the message includes a source or destination port
 *     number.
 *
 * @return The number of transport-layer segments required to send the message.
 */
KNIEXPORT KNI_RETURNTYPE_INT
Java_com_sun_cldc_io_j2me_sms_Protocol_numberOfSegments0(void) {
    jint segments = 0;
#if ENABLE_PCSL
    unsigned char* msgBuffer = NULL;
    int msgLen = 0;
    int msgType = 0;
    jboolean hasPort = KNI_FALSE;
    WMA_STATUS status;


    KNI_StartHandles(1);
    KNI_DeclareHandle(msgBufferObject);

    KNI_GetParameterAsObject(1, msgBufferObject);
    msgLen = KNI_GetParameterAsInt(2);
    msgType = KNI_GetParameterAsInt(3);
    hasPort = KNI_GetParameterAsBoolean(4);

    if (!KNI_IsNullHandle(msgBufferObject)) {

        /*
         * Pick up the length of the message, which should be the same as
         * <code>msgLen</code>. This is just done here as a formality.
         */
        int length = KNI_GetArrayLength(msgBufferObject);

        if (length > 0) {
            msgBuffer = (unsigned char *)pcsl_mem_malloc(length);
            memset(msgBuffer, 0, length);
            KNI_GetRawArrayRegion(msgBufferObject, 0, length, (jbyte*)msgBuffer);
	}
	/* Compute the number of segments required to send the message. */
	status = jsr120_number_of_sms_segments(msgBuffer, msgLen, msgType,
					       hasPort, &segments);

	/* Limit SMS messages to 3 segments. */
    if ((status != WMA_OK) && (segments > 3)) {
	  segments = 0;
	}
    }

    pcsl_mem_free(msgBuffer);
    KNI_EndHandles();
#endif
    KNI_ReturnInt(segments);
}

/*
 * Native finalization of the Java object.
 *
 * CLASS:    com.sun.midp.io.j2me.sms.Protocol
 * TYPE:     virtual native function
 * INTERFACE (operand stack manipulation):
 */
KNIEXPORT KNI_RETURNTYPE_VOID
Java_com_sun_cldc_io_j2me_sms_Protocol_finalize(void) {
#if ENABLE_PCSL
    int port;
    int handle;
	int mode;
	jboolean flag;
    jboolean isOpen;
	int deReg;

    KNI_StartHandles(2);
    KNI_DeclareHandle(thisPointer);
    KNI_DeclareHandle(thisClass);

    KNI_GetThisPointer(thisPointer);
    KNI_GetObjectClass(thisPointer, thisClass);
    isOpen = KNI_GetBooleanField(thisPointer, KNI_GetFieldID(thisClass, "open", "Z"));

    if (isOpen) {
        port = KNI_GetIntField(thisPointer, KNI_GetFieldID(thisClass, "m_iport", "I"));
        handle = KNI_GetIntField(thisPointer, KNI_GetFieldID(thisClass, "connHandle", "I"));
		mode = KNI_GetIntField(thisPointer, KNI_GetFieldID(thisClass, "m_mode", "I"));

		if ((mode & 1) != 0) {
			flag = KNI_TRUE;
			deReg = 1;
		} else {
			flag = KNI_FALSE;
			deReg = 0;
		}

        closeConnection(port, handle, deReg, flag);
    }

    KNI_EndHandles();
#endif
    KNI_ReturnVoid();
}

