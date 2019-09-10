use crate::{foreign::Source, Error};
use std::fs;
use std::io;
use std::os::unix::fs::OpenOptionsExt;
use std::path::Path;
use tar::Archive;

pub fn unpack_tarfile(source: Source, dest_dir: &str) -> Result<(), Error> {
    let dest_dir = Path::new(dest_dir);

    let mut tar = Archive::new(source);

    for file in tar.entries().unwrap() {
        let mut file = file.unwrap();

        let dest_file = dest_dir.join(file.path().unwrap());

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
            tar::EntryType::Symlink => {
                std::os::unix::fs::symlink(file.header().link_name().unwrap().unwrap(), dest_file)
                    .unwrap();
            }
            t => return Err(Error::Misc(format!("unsupported tar entry type '{:?}'", t))),
        }
    }

    Ok(())
}
