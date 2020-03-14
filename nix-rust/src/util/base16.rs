use crate::error::Error;
use lazy_static::lazy_static;

// Returns the length of a base-16 representation of this hash.
pub fn encoded_len(input_len: usize) -> usize {
    input_len * 2
}

pub fn decoded_len(input_len: usize) -> usize {
    input_len / 2
}

static BASE16_CHARS: &'static [u8; 16] = &b"0123456789abcdef";

lazy_static! {
    static ref BASE16_CHARS_REVERSE: Box<[u8; 256]> = {
        let mut xs = [0xffu8; 256];
        for (n, c) in BASE16_CHARS.iter().enumerate() {
            xs[*c as usize] = n as u8;
        }
        Box::new(xs)
    };
}

pub fn encode(input: &[u8]) -> String {
    let mut buf = vec![0; encoded_len(input.len())];
    encode_into(input, &mut buf);
    std::str::from_utf8(&buf).unwrap().to_string()
}

pub fn encode_into(input: &[u8], output: &mut [u8]) {
    for i in 0..output.len() {
        output[i * 2] = BASE16_CHARS[(input[i] >> 4) as usize];
        output[i * 2 + 1] = BASE16_CHARS[(input[i] & 0x0f) as usize];
    }
}

pub fn decode(input: &str) -> Result<Vec<u8>, crate::Error> {
    if input.len() & 2 != 0 {
        return Err(Error::BadBase16);
    }

    let mut res = Vec::with_capacity(decoded_len(input.len()));

    let mut bits_left: usize = 0;

    let mut parity = true;

    for c in input.chars() {
        parity = !parity;
        let b = BASE16_CHARS_REVERSE[c as usize];
        if b == 0xff {
            return Err(Error::BadBase16);
        }
        if parity {
            res.push(b >> 4)
        } else {
            *res.last_mut().unwrap() |= b;
        }
        if parity {
            bits_left += 1;
        }
    }

    Ok(res)
}

#[cfg(test)]
mod tests {
    use super::*;
    use assert_matches::assert_matches;
    use hex;
    use proptest::proptest;

    #[test]
    fn test_encode() {
        assert_eq!(encode(&[]), "");

        assert_eq!(
            encode(&hex::decode("0839703786356bca59b0f4a32987eb2e6de43ae8").unwrap()),
            "x0xf8v9fxf3jk8zln1cwlsrmhqvp0f88"
        );

        assert_eq!(
            encode(
                &hex::decode("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
                    .unwrap()
            ),
            "1b8m03r63zqhnjf7l5wnldhh7c134ap5vpj0850ymkq1iyzicy5s"
        );

        assert_eq!(
            encode(
                &hex::decode("ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f")
                    .unwrap()
            ),
            "2gs8k559z4rlahfx0y688s49m2vvszylcikrfinm30ly9rak69236nkam5ydvly1ai7xac99vxfc4ii84hawjbk876blyk1jfhkbbyx"
        );
    }

    #[test]
    fn test_decode() {
        assert_eq!(hex::encode(decode("").unwrap()), "");

        assert_eq!(
            hex::encode(decode("x0xf8v9fxf3jk8zln1cwlsrmhqvp0f88").unwrap()),
            "0839703786356bca59b0f4a32987eb2e6de43ae8"
        );

        assert_eq!(
            hex::encode(decode("1b8m03r63zqhnjf7l5wnldhh7c134ap5vpj0850ymkq1iyzicy5s").unwrap()),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
        );

        assert_eq!(
            hex::encode(decode("2gs8k559z4rlahfx0y688s49m2vvszylcikrfinm30ly9rak69236nkam5ydvly1ai7xac99vxfc4ii84hawjbk876blyk1jfhkbbyx").unwrap()),
            "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f"
        );

        assert_matches!(
            decode("xoxf8v9fxf3jk8zln1cwlsrmhqvp0f88"),
            Err(Error::BadBase16)
        );
        assert_matches!(
            decode("2b8m03r63zqhnjf7l5wnldhh7c134ap5vpj0850ymkq1iyzicy5s"),
            Err(Error::BadBase16)
        );
        assert_matches!(decode("2"), Err(Error::BadBase16));
        assert_matches!(decode("2gs"), Err(Error::BadBase16));
        assert_matches!(decode("2gs8"), Err(Error::BadBase16));
    }

    proptest! {

        #[test]
        fn roundtrip(s: Vec<u8>) {
            assert_eq!(s, decode(&encode(&s)).unwrap());
        }
    }
}
