# GWU-Sat Digipeater

A digipeater acts as a radio station specifically tailored towards ham radio operators operating on the 2m band and utilizing the Automated Packet Reporting System. 
This digipeater does some things differently. 

| Settings | Normal Digipeater | GWU-Sat |
| -------- | -------- | -------- |
| Frequency | 144.39 MHz | 436.42 MHz |
| Bit Rate | Usually 1200 | Variable |
| Modulation | AFSK | CSS |

While these settings are different it still offers the same behaviors of a normal digipeating station capturing messages and repeating them. 
The reseaoning for the biggest change of our system (frequency modulation) is decided by the lora board we decided to use. 
Modern micro controllers already use digital keys thus there is no need to convert from audio to digital like older digipeaters. 


<img width="2384" height="1782" alt="Screenshot 2026-06-04 at 9 47 45 AM" src="https://github.com/user-attachments/assets/39332ed0-0ac9-441d-9f24-7a678860eb13" />
