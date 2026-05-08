# Commit History (pre-reinit snapshot)

commit 4e7685232d77467b3d7879be2d454ef4a8712735
Author:     alifuatakyemis <ali.fuat.akyemis@gmail.com>
AuthorDate: Fri May 8 13:28:57 2026 +0300
Commit:     alifuatakyemis <ali.fuat.akyemis@gmail.com>
CommitDate: Fri May 8 13:28:57 2026 +0300

    delete: ReadMe.adoc deleted

 ReadMe.adoc | 58 ----------------------------------------------------------
 1 file changed, 58 deletions(-)

commit faf3c9cb32870f15badc6986a567723c22be277b
Author:     alifuatakyemis <ali.fuat.akyemis@gmail.com>
AuthorDate: Fri May 8 13:25:58 2026 +0300
Commit:     alifuatakyemis <ali.fuat.akyemis@gmail.com>
CommitDate: Fri May 8 13:25:58 2026 +0300

    docs: add production-ready README and MIT license
    
    Turkish-language README with Mermaid architecture/flow/state diagrams,
    full hardware pinout, cloud variable reference, and security notes.
    
    Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>

 LICENSE   |  21 ++++
 README.md | 330 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 2 files changed, 351 insertions(+)

commit 8310508ee1afdfb2d64813216a869f682cb1a644
Author:     alifuatakyemis <ali.fuat.akyemis@gmail.com>
AuthorDate: Fri May 8 10:16:27 2026 +0300
Commit:     alifuatakyemis <ali.fuat.akyemis@gmail.com>
CommitDate: Fri May 8 10:16:27 2026 +0300

    refactor: centralize alarm reset and boot in unlocked state
    
    - Add resetAlarmState() helper; replaces duplicated alarm-clear logic
      in onAlarmResetChange, onRemoteUnlockChange, RFID enroll path
    - unlockVault() calls resetAlarmState() so every unlock path silences
      the buzzer (fixes RFID-auth unlock not stopping motion alarm)
    - Boot now starts in unlocked state instead of locked
    
    Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>

 SecureIoT_Transit_Vault.ino | 26 ++++++++++++++------------
 1 file changed, 14 insertions(+), 12 deletions(-)

commit fb3d1922038477fc007a8bb132405eb1388d65d3
Author:     alifuatakyemis <ali.fuat.akyemis@gmail.com>
AuthorDate: Fri May 8 10:06:15 2026 +0300
Commit:     alifuatakyemis <ali.fuat.akyemis@gmail.com>
CommitDate: Fri May 8 10:06:15 2026 +0300

    refactor: Unnecessary timezone calculation removed

 SecureIoT_Transit_Vault.ino | 7 +++----
 1 file changed, 3 insertions(+), 4 deletions(-)

commit d339f9434f74f81fff5b986adc30eb90cd0e5512
Author:     alifuatakyemis <ali.fuat.akyemis@gmail.com>
AuthorDate: Fri May 8 10:02:08 2026 +0300
Commit:     alifuatakyemis <ali.fuat.akyemis@gmail.com>
CommitDate: Fri May 8 10:02:08 2026 +0300

    feat: alarm trigger disabled when lockStatus is false

 SecureIoT_Transit_Vault.ino | 2 +-
 1 file changed, 1 insertion(+), 1 deletion(-)

commit 79da6684fd4cbc70b48aa57109c21bb4cb6f5245
Author:     alifuatakyemis <ali.fuat.akyemis@gmail.com>
AuthorDate: Thu May 7 16:47:44 2026 +0300
Commit:     alifuatakyemis <ali.fuat.akyemis@gmail.com>
CommitDate: Thu May 7 16:47:44 2026 +0300

    feat(stats): add MPU-6050 session statistics to cloud dashboard
    
    Track cumulative avg/max for shake (dynamic gForce), absolute tilt,
    and relative tilt (baseline-referenced). Baseline resets on boot and
    each lock. statsReset cloud variable clears counters and refreshes
    baseline on demand. Publishes at 1 Hz to avoid cloud spam.
    
    Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>

 SecureIoT_Transit_Vault.ino | 100 ++++++++++++++++++++++++++++++++++++++++++++
 thingProperties.h           |  35 ++++++++++++----
 2 files changed, 128 insertions(+), 7 deletions(-)

commit a68c195bf8f0f6724a4976fb2146bc11f34ed6aa
Author:     alifuatakyemis <ali.fuat.akyemis@gmail.com>
AuthorDate: Thu May 7 15:49:45 2026 +0300
Commit:     alifuatakyemis <ali.fuat.akyemis@gmail.com>
CommitDate: Thu May 7 15:49:45 2026 +0300

    feat(oled): redesign display layout with icons and status regions
    
    Three-zone layout: top bar (WiFi icon + centered HH:MM UTC+3 +
    lock icon), large centered status text (LOCKED/UNLOCKED/ALARM),
    bottom sensor row (Tilt G-force left, angle right).
    
    Replaces text-only WiFi label with 3-bar pixel icon and padlock
    pixel icon reflecting live lock state.

 SecureIoT_Transit_Vault.ino | 81 +++++++++++++++++++++++++++++++++------------
 1 file changed, 60 insertions(+), 21 deletions(-)

commit 257d2b8e41c08ca208cc4d098dd4cb798fc35ce0
Author:     alifuatakyemis <ali.fuat.akyemis@gmail.com>
AuthorDate: Thu May 7 15:28:37 2026 +0300
Commit:     alifuatakyemis <ali.fuat.akyemis@gmail.com>
CommitDate: Thu May 7 15:28:37 2026 +0300

    refactor: Buzzer alarm reduced to 1 second

 SecureIoT_Transit_Vault.ino | 10 +++++-----
 1 file changed, 5 insertions(+), 5 deletions(-)

commit 3a53280454f521d03d162745f4ef8aa70427e933
Author:     alifuatakyemis <ali.fuat.akyemis@gmail.com>
AuthorDate: Mon May 4 17:39:33 2026 +0300
Commit:     alifuatakyemis <ali.fuat.akyemis@gmail.com>
CommitDate: Mon May 4 17:42:10 2026 +0300

    feat: Persistent storage and authorized uid management corrected

 .gitignore                  |  1 +
 SecureIoT_Transit_Vault.ino | 87 ++++++++++++++++++++++++++++++++++-----------
 2 files changed, 68 insertions(+), 20 deletions(-)

commit 61fca33a9681c8e6ab5bf19f14a794921e58e65d
Author:     alifuatakyemis <ali.fuat.akyemis@gmail.com>
AuthorDate: Mon May 4 16:34:12 2026 +0300
Commit:     alifuatakyemis <ali.fuat.akyemis@gmail.com>
CommitDate: Mon May 4 16:34:12 2026 +0300

    feat: remote unlock toggle matches RFID lock/unlock behavior
    
    Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>

 SecureIoT_Transit_Vault.ino | 14 ++++++++++++--
 1 file changed, 12 insertions(+), 2 deletions(-)

commit b926e2a8357cce40635ded6e5f861e7dd145d226
Author:     alifuatakyemis <ali.fuat.akyemis@gmail.com>
AuthorDate: Mon May 4 16:26:32 2026 +0300
Commit:     alifuatakyemis <ali.fuat.akyemis@gmail.com>
CommitDate: Mon May 4 16:26:32 2026 +0300

    feat: RFID toggle lock/unlock with debounce and unauthorized buzzer alert
    
    - Authorized card toggles vault: locked→unlock, unlocked→lock+full reset
    - Lock reset clears alarm, buzzer, and disturbance timestamp
    - Unauthorized card triggers non-blocking 3s buzzer warning
    - 2s debounce prevents double-scan on single card presentation
    
    Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>

 SecureIoT_Transit_Vault.ino | 31 ++++++++++++++++++++++++++++---
 1 file changed, 28 insertions(+), 3 deletions(-)

commit 73e2edc0610d73487cf8046f3163e302b69c32b7
Author:     alifuatakyemis <ali.fuat.akyemis@gmail.com>
AuthorDate: Mon May 4 16:06:10 2026 +0300
Commit:     alifuatakyemis <ali.fuat.akyemis@gmail.com>
CommitDate: Mon May 4 16:06:10 2026 +0300

    fix: RFID unlock path causing ESP32 brownout reboot
    
    Close RFID transaction (PICC_HaltA + PCD_StopCrypto1) before writing
    servo position so RF antenna and SPI bus are idle when servo draws
    inrush current. Add brownout detector disable and reset reason logging
    to diagnose remaining power issues.
    
    Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>

 SecureIoT_Transit_Vault.ino | 16 ++++++++++++----
 1 file changed, 12 insertions(+), 4 deletions(-)

commit 442a90dee2f52b596e0666189a197417ecae95ac
Author:     alifuatakyemis <ali.fuat.akyemis@gmail.com>
AuthorDate: Mon May 4 16:05:02 2026 +0300
Commit:     alifuatakyemis <ali.fuat.akyemis@gmail.com>
CommitDate: Mon May 4 16:05:02 2026 +0300

    feat: Project milestone

 .gitignore                  |   3 +
 CLAUDE.md                   |  61 +++++++++
 README.md                   |   1 -
 ReadMe.adoc                 |  58 +++++++++
 SecureIoT_Transit_Vault.ino | 308 ++++++++++++++++++++++++++++++++++++++++++++
 circuit-diagram.txt         |  40 ++++++
 esp32s-pin-diagram.png      | Bin 0 -> 300335 bytes
 sketch.json                 |  43 +++++++
 thingProperties.h           |  39 ++++++
 9 files changed, 552 insertions(+), 1 deletion(-)

commit 0cd12d7d8a956bf2a389b193319b7bc7f89d6920
Author:     alifuatakyemis <ali.fuat.akyemis@gmail.com>
AuthorDate: Wed Apr 22 23:05:29 2026 +0300
Commit:     alifuatakyemis <ali.fuat.akyemis@gmail.com>
CommitDate: Wed Apr 22 23:05:29 2026 +0300

    first commit

 README.md | 1 +
 1 file changed, 1 insertion(+)
