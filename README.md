Window-Based Reliable Data Transfer Over UDP
============================================

Selective Repeat (SR) protocol
------------------------------
- Use one timeout for entire window of sent packets
- Resend packets which are not ACK’d
- Move window by number of ACK’s and send new packets

Overview
--------
We implemented reliable data transfer using the **Selective-Repeat (SR)** protocol. In our design we used one timer for the entire window instead of a separate timer for each packet (i.e. on timeout we resend all the packets for which we have not received ACKs for).

A simple walkthrough of the file transfer process:

1. Receiver sends a QRY packet to the sender (repeatedly until it receives a response)
2. Sender opens the file listed in the data field of the packet
  - If it exists send n DATA packets (where n is the window size)
  - If it doesn’t exist then send a BAD packet and close the sender
3. Receiver writes the packets to the file if they are in sequence
  - If they are not in sequence, the receiver will save them to a secondary buffer
4. Receiver sends ACKS for all received packets
5. Sender takes note of all the ACKS until timeout
6. At timeout the sender resends all the packet that weren’t ACK’d
7. Then the sender moves the window by w (where w is the number of ACK’s) and sends the new
packets
8. Repeat steps 3 through 8 until all packets have been transmitted
9. The sender sends a FIN packet to let the receiver know it has completed the transfer
10. The receiver replies with FIN_ACK message and both exit

Implementation
--------------
Our sender and receiver network applications communicated with one another using a common packet data structure which we defined as follows:
```
typedef struct {
    int type; // DATA, QRY, ACK, DATA, FIN, FIN_ACK, BAD, MSG
    unsigned int seq; unsigned int size;
    char data[DATA_SIZE];
    } Packet;
```
**type** – We used 8 different types of packets: QRY – initial file query

**ACK** – Acknowledgment of packet delivery (from receiver to sender)

**DATA** – A part of the file (from sender to receiver)

**FIN** – Finished file transfer (from sender to receiver)

**FIN_ACK** – Acknowledge of completed file transfer (from receiver to sender)
**BAD** – A notice that the file does not exist (from receiver to sender)

**MSG** – A notice that the end of the file has been reached (from receiver to sender)

**seq** – The sequence number distinguishes one packet from another. It increases based on the size of the packet (i.e. if the first sequence number of 0, the next packet of 1500 bytes would have a sequence number of 1500)

**size** – The size of the data packet

**data** – A piece of the requested file, the data packet
