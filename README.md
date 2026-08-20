# EDUK2-TCC

## Compilar e executar o solver faithful

A partir do diretório `~/TCC/EDUK2-TCC`, gere os arquivos de compilação no
diretório ignorado pelo Git `eduk2_cpp/build/` e compile o projeto:

```bash
cd ~/TCC/EDUK2-TCC
cmake -S eduk2_cpp -B eduk2_cpp/build -DCMAKE_BUILD_TYPE=Release
cmake --build eduk2_cpp/build -j
```

Para executar o solver `faithful` com a instância de exemplo:

```bash
./eduk2_cpp/build/ukp_solve faithful eduk2_cpp/data/example.ukp
```

Substitua `eduk2_cpp/data/example.ukp` pelo caminho da instância que deseja
resolver. Para exibir o diagnóstico detalhado de cada `slice`, acrescente
`--verbose` ao final do comando.
