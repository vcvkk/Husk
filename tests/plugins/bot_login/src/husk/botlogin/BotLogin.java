package husk.botlogin;

import android.util.Log;

import java.util.ArrayList;

import org.telegram.messenger.AndroidUtilities;
import org.telegram.messenger.BuildVars;
import org.telegram.messenger.MessagesController;
import org.telegram.messenger.MessagesStorage;
import org.telegram.messenger.UserConfig;
import org.telegram.tgnet.ConnectionsManager;
import org.telegram.tgnet.InputSerializedData;
import org.telegram.tgnet.OutputSerializedData;
import org.telegram.tgnet.RequestDelegate;
import org.telegram.tgnet.TLObject;
import org.telegram.tgnet.TLRPC;

/**
 * auth.importBotAuthorization (the app has no class of its own for it), and with its answer what LoginActivity.onAuthSuccess
 * does: the account is the bot's from then on. Its screens show from the app's next start.
 */
public final class BotLogin extends TLObject implements java.util.function.Consumer<String> {
    static final String TAG = "husk_bot_login";
    private static final int CONSTRUCTOR = 0x67a3ff2c;
    /* ConnectionsManager.RequestFlagFailOnServerErrors | RequestFlagWithoutLogin, as LoginActivity's requests */
    private static final int FLAGS = 2 | 8;
    private String token;

    public BotLogin() {}

    @Override public TLObject deserializeResponse(InputSerializedData stream, int constructor, boolean exception) {
        return TLRPC.auth_Authorization.TLdeserialize(stream, constructor, exception);
    }

    @Override public void serializeToStream(OutputSerializedData stream) {
        stream.writeInt32(CONSTRUCTOR);
        stream.writeInt32(0);
        stream.writeInt32(BuildVars.getExteraAppId());
        stream.writeString(BuildVars.getExteraAppHash());
        stream.writeString(token);
    }

    /** The plugin's call: logs in with the bot's token unless the account is logged in already. */
    @Override public void accept(String botToken) {
        final int account = UserConfig.selectedAccount;
        if (UserConfig.getInstance(account).isClientActivated()) { Log.i(TAG, "logged in already"); return; }
        token = botToken;
        Log.i(TAG, "importing the bot's authorization");
        ConnectionsManager.getInstance(account).sendRequest(this, new RequestDelegate() {
            @Override public void run(final TLObject response, final TLRPC.TL_error error) {
                AndroidUtilities.runOnUIThread(new Runnable() {
                    @Override public void run() {
                        if (response instanceof TLRPC.TL_auth_authorization) loggedIn(account, (TLRPC.TL_auth_authorization) response);
                        else Log.e(TAG, error != null ? "failed: " + error.code + " " + error.text : "failed: " + response);
                    }
                });
            }
        }, FLAGS);
    }

    private static void loggedIn(int account, TLRPC.TL_auth_authorization res) {
        MessagesController.getInstance(account).cleanup();
        ConnectionsManager.getInstance(account).setUserId(res.user.id);
        UserConfig.getInstance(account).clearConfig();
        MessagesController.getInstance(account).cleanup();
        UserConfig.getInstance(account).syncContacts = false;
        UserConfig.getInstance(account).setCurrentUser(res.user);
        UserConfig.getInstance(account).saveConfig(true);
        MessagesStorage.getInstance(account).cleanup(true);
        ArrayList<TLRPC.User> users = new ArrayList<>();
        users.add(res.user);
        MessagesStorage.getInstance(account).putUsersAndChats(users, null, true, true);
        MessagesController.getInstance(account).putUser(res.user, false);
        ConnectionsManager.getInstance(account).updateDcSettings();
        MessagesController.getInstance(account).loadAppConfig();
        Log.i(TAG, "logged in as @" + res.user.username + " (" + res.user.id + ")");
    }
}
