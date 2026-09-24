# Replace the Windows-side test QEMU (name plan2001test) with a fresh one that
# boots C:\temp\esp.  Called by tools/test-qemu.sh after a PASS.
Get-CimInstance Win32_Process -Filter "name='qemu-system-x86_64.exe'" |
  Where-Object { $_.CommandLine -match '-name plan2001test' } |
  ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
Start-Sleep -Seconds 1
Start-Process -FilePath "C:\Program Files\qemu\qemu-system-x86_64.exe" -ArgumentList @(
  "-name","plan2001test","-accel","whpx,kernel-irqchip=off","-machine","q35","-cpu","qemu64",
  "-smp","2","-m","2048","-bios","C:\VM\9front\OVMF.fd","-vga","std","-display","gtk",
  "-drive","file=fat:rw:C:\temp\esp,format=raw,if=none,id=esp",
  "-device","ide-hd,drive=esp,bootindex=0")
