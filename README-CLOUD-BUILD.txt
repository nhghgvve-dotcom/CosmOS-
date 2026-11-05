COSMOS CLOUD BUILD (NO UBUNTU INSTALL ON YOUR LAPTOP)

1. Create a GitHub repository named CosmOS.
2. Upload the contents of this folder to the repository (including the hidden .github folder).
3. Open the repository's Actions tab.
4. Select "Build CosmOS UEFI ISO" and click "Run workflow".
5. Wait for the workflow to finish successfully.
6. Open the completed workflow run and download the "CosmOS-UEFI-ISO" artifact.
7. Extract the downloaded artifact ZIP. It contains CosmOS.iso.
8. In VirtualBox create a new VM, choose Other/Unknown 64-bit if available, assign 2 GB RAM,
   enable EFI under Settings > System > Motherboard, attach CosmOS.iso to the optical drive,
   and boot it. Use a new disposable VM; do not install this prototype on your laptop's disk.

This builds the existing independent x86-64 kernel prototype using Limine as its bootloader.
It is not yet a full desktop, installer, filesystem, or app store. The workflow is a cloud
build recipe; it does not mean an ISO has already been built.
