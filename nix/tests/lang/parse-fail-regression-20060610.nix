let {
  x =
    {gcc}:
    {
      inherit gcc;
    };

  body = ({
    inherit gcc;
  }).gcc;
}
