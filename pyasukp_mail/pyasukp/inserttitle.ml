if Array.length Sys.argv <3 then prerr_endline "no title inserted"
else
  begin
    let ch_in = open_in Sys.argv.(1) in
    let tmp = (Filename.temp_file "insertt" "tex") in
    let ch_out = open_out tmp in 
    let rs = ref (input_line ch_in) in
    while !rs <> "\\documentclass[12pt]{article}"do
      output_string ch_out (!rs ^"\n");
      rs := input_line ch_in
    done;
    output_string ch_out (!rs ^"\n");
    rs := input_line ch_in;
    output_string ch_out ("\\usepackage{hevea}\n");
    output_string ch_out ("\\usepackage{moreverb}\n");
    while !rs <> "\\begin{document}"do
      output_string ch_out (!rs ^"\n");
      rs := input_line ch_in
    done;
    output_string ch_out ("\\input{"^Sys.argv.(2)^"}\n");
      output_string ch_out (!rs ^"\n");
    output_string ch_out ("\\maketitle\n\\tableofcontents\n");
    try while true do
      rs := input_line ch_in;
      output_string ch_out (!rs ^"\n")
    done
    with End_of_file ->
      begin
        close_out ch_out;
        close_in ch_in
      end;
  exit(Sys.command  ("cp "^tmp ^" "^Sys.argv.(1)))

  end
