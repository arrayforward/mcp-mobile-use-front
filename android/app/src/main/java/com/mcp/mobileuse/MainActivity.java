package com.mcp.mobileuse;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.Spinner;
import android.widget.TextView;

public class MainActivity extends Activity {

    private EditText editPort;
    private Spinner spinnerBackend;
    private TextView textStatus;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        editPort = findViewById(R.id.editPort);
        spinnerBackend = findViewById(R.id.spinnerBackend);
        textStatus = findViewById(R.id.textStatus);
        Button btnStart = findViewById(R.id.btnStart);
        Button btnStop = findViewById(R.id.btnStop);

        ArrayAdapter<String> adapter = new ArrayAdapter<>(
                this, android.R.layout.simple_spinner_item,
                new String[]{"adb", "cloud"});
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        spinnerBackend.setAdapter(adapter);

        btnStart.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                int port;
                try {
                    port = Integer.parseInt(editPort.getText().toString().trim());
                } catch (NumberFormatException e) {
                    textStatus.setText("invalid port");
                    return;
                }
                String backend = (String) spinnerBackend.getSelectedItem();
                Intent intent = new Intent(MainActivity.this, McpForegroundService.class);
                intent.putExtra(McpForegroundService.EXTRA_PORT, port);
                intent.putExtra(McpForegroundService.EXTRA_BACKEND, backend);
                startForegroundService(intent);
                textStatus.setText("running on :" + port + " (" + backend + ")");
            }
        });

        btnStop.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                stopService(new Intent(MainActivity.this, McpForegroundService.class));
                textStatus.setText("stopped");
            }
        });
    }
}
