extern crate libc;
extern crate tar;

use std::fs;
use std::io;
use std::os::unix::fs::OpenOptionsExt;
use std::path::Path;
use tar::Archive;

#[no_mangle]
pub extern "C" fn unpack_tarfile(data: &[u8], dest_dir: &str) -> bool {
    // FIXME: handle errors.

    let dest_dir = Path::new(dest_dir);

    let mut tar = Archive::new(data);

    for file in tar.entries().unwrap() {
        let mut file = file.unwrap();

        let dest_file = dest_dir.join(file.header().path().unwrap());

        fs::create_dir_all(dest_file.parent().unwrap()).unwrap();

        match file.header().entry_type() {
            tar::EntryType::Directory => {
                fs::create_dir(dest_file).unwrap();
            }
            tar::EntryType::Regular => {
                let mode = if file.header().mode().unwrap() & libc::S_IXUSR == 0 {
                    0o666
                } else {
                    0o777
                };
                let mut f = fs::OpenOptions::new()
                    .create(true)
                    .write(true)
                    .mode(mode)
                    .open(dest_file)
                    .unwrap();
                io::copy(&mut file, &mut f).unwrap();
            }
            t => panic!("Unsupported tar entry type '{:?}'.", t),
        }
    }

    true
}
