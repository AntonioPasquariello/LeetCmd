# LeetCmd
 WD My Book Elite Display Linux Client

An update implementation of WD Elite Book Display Utility

The original code and info below came from [WD e-label](https://basicmaster.de/wd_e-label/).

## Description
This tool implements the protocol stated below and allows to modify the label and the free space information. Furthermore it is possible to enable/disable the virtual CD (VCD) or the display inversion. Note: This tool is not related in any way to WD.

## Protocol
All communication regarding the drive is done through the SCSI Enclosure Services (SES) device which belongs to the drive. The specific settings can be read/modified by using vendor-independent commands and vendor-specific parameters.
To not lock me out myself from my drive I did not take a look at the encryption function. At least I know that the lock symbol cannot be enabled/disabled seperately.

Notes:
 + b7 refers to the regarding MSB, b0 to the LSB.
 + greyed out bytes are not relevant and can be ignored

## Disable VCD flag
Initially when the drive is connected to a computer, an additional read-only CD drive appears which contains drivers and manuals. This Virtual CD (VCD) can be disabled via a flag.

Use the MODE SENSE command for reading and the MODE SELECT command for writing. The flag resides in mode page 0x20, sub-page 0x00. The page content is 6 bytes long; the flag itself is the 22th bit within the page content.

Mode page 0x20, sub-page 0x00

| Byte | offset	Meaning |
| --- | --- |
| 0 | |
| 1 | |
| 2 |b1 = Disable VCD flag |
| 3 | |
| 4 | |
| 5 | |

## Inverse Display flag
The display as a whole can optionally be displayed inverse via a flag.

Use the MODE SENSE command for reading and the MODE SELECT command for writing. The flag resides in mode page 0x21, sub-page 0x00. The page content is 10 bytes long; the flag itself is the 71th bit within the page content.

Mode page 0x21, sub-page 0x00

| Byte | offset	Meaning |
| --- | --- |
| 0	| |
| 1	| |
| 2	| |
| 3	| |
| 4	| |
| 5	| |
| 6	| |
| 7	| |
| 8	| b0 = Inverse Display flag |
| 9	| |

## Custom label
The user-customizable text label is stored in parallel in two different ways:
in plain text, so that WD SmartWare can offer the old value to the user for editing and for comparison to prevent unnecessary label changing
in encoded form to drive the display segments of the 14-segment display digits

### Plain text
The plain text is read/written by vendor-specific commands.

### Display segments
Use the RECEIVE DIAGNOSTIC RESULTS command for reading and the SEND DIAGNOSTIC command for writing. The data resides in diagnostic page 0x87. The page content is 32 bytes long; the label itself starts at offset 8 and uses two bytes per character.

Diagnostic page 0x87

| Byte offset |	Meaning |	Byte offset |	Meaning |
| --- | --- | --- | --- |
| 0	| |	16 |	Label char 5 |
| 1	| |	17 | Label char 5 |
| 2	| |	18 |	Label char 6 |
| 3	| |	19 | Label char 6 |
| 4	| |	20	| Label char 7 |
| 5	| |	21 | Label char 7 |
| 6	| |	22	| Label char 8 |
| 7	| |	23 | Label char 8 |
| 8	| Label char 1	| 24	| Label char 9 |
| 9	| Label char 1	| 25 | Label char 9	|
| 10	| Label char 2	| 26	| Label char 10 |
| 11	| Label char 2	| 27	| Label char 10 |
| 12	| Label char 3	| 28	| Label char 11 |
| 13	| Label char 3	| 29	| Label char 11 |
| 14	| Label char 4	| 30	| Label char 12 |
| 15	| Label char 4	| 31	| Label char 12 |

The mapping of each segment to the regarding bit is shown in the following image (0 in the image equals to b0 of the 16bit value, 1 to b1 etc.).

<img width="180" height="300" alt="Mapping of bits to segments" src="https://github.com/user-attachments/assets/a4ecafe9-6654-4b03-8c39-e462818d51d7" />

## Free space
The free space fields are cleared after some seconds whenever the drive is connected to a system which does not run WD SmartWare. The regarding page therefore does not contain the present state - this also applies after the fields were updated by WD SmartWare. Therefore it is not possible to read out any previously written field content.
Use the RECEIVE DIAGNOSTIC RESULTS command for reading (a meaningless default) and the SEND DIAGNOSTIC command for writing. The data resides in diagnostic page 0x86. The page content is 16 bytes long.

Diagnostic page 0x86

| Byte offset	| Meaning |
| --- | --- |
| 0	|	(meaning unknown; must be 0x33!)	|
| 1	|	(meaning unknown; must be 0x0A!)	|
| 2	|	(meaning unknown; must be 0x03!)	|
| 3	|	(meaning unknown; must be 0x00!)	|
| 4	|	b7 = segment frame (needed for any segment!)	|
| 5	|	|
| 6	|	b15-b6 = segments (MSB = most-left/top one)	|
| 7	| b15-b6 = segments (MSB = most-left/top one)	|
| 8	|	|
| 9	|	|
| 10	|	b4 = FREE indicator; b2 = TB indicator; b1 = GB indicator	|
| 11	|	b1 = decimal point between free space 100s and 10s	|
| 12	|	b7 = show digit; b3-b0 = BCD value (free space 100s)	|
| 13	|	b7 = show digit; b3-b0 = BCD value (free space 10s)	|
| 14	|	b7 = show digit; b3-b0 = BCD value (free space 1s)	|
| 15	|	|

## Security lock
It seems that the security lock symbol is only displayed when encryption is enabled. Thus it is not possible to fake enabled encryption while it is in fact disabled. When a password is set, besides some other communication the password question is stored to the drive.
