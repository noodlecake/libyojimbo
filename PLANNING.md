PLANNING
========

-------------------

Implement packet aggregation in the base interface (packet processor?)

-------------------

Implement packet fragmentation and reassembly in the base interface (packet processor?)

-------------------

Bring across the reliable ordered event system and large block sender.

Store this reliability level system inside the client/server, have one of these endpoints per-client slot.

Consider how the ack system is going to integrate. Custom ack header with bits and so on per-packet? (aggregate packet header?)

Acks on a message id level instead of acks on a packet level? Who knows. A packet id that increases for each *message* packet sent, with packet header just for that packet type? Sure. This is it. This way I don't need to send message packets unless there are actually messages to be sent.

The whole message packet system can be self contained with no bullshit. Awesome. 

Combine the ack system and the message packet into one system!

-------------------

If I ever need NAT punchthrough, libjingle looks awesome

https://developers.google.com/talk/libjingle/developer_guide#organization-of-the-sdk

-------------------
